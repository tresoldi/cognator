#include "internal/data.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct cg_adj_entry {
    uint64_t target;
    double weight;
} cg_adj_entry;

typedef struct cg_neighbor {
    uint64_t node;
    double distance;
} cg_neighbor;

typedef struct cg_node_profile {
    double best_weight;
    double second_weight;
} cg_node_profile;

typedef struct cg_support_profile {
    double in_mean;
    double in_total;
    uint64_t in_count;
    uint64_t best_alt_mod;
    double best_alt_mean;
    double best_alt_total;
    uint64_t best_alt_count;
    int has_alt;
} cg_support_profile;

struct cg_graph {
    uint64_t node_count;
    cg_adj_entry **adj;
    uint64_t *adj_count;
    uint64_t *adj_cap;
    double *strength;
    double w;
};

struct cg_partition {
    uint64_t *modules;
    uint64_t count;
};

struct cg_diagnostics {
    cg_diagnostic *items;
    uint64_t count;
};

struct cg_bootstrap_summary {
    double *mean;
    double *low;
    double *high;
    uint64_t count;
};

static double similarity(double distance) {
    if (distance < 0.0) {
        distance = 0.0;
    }
    if (distance > 1.0) {
        distance = 1.0;
    }
    return 1.0 - distance;
}

static double min_double(double a, double b) {
    return a < b ? a : b;
}

static double max_double(double a, double b) {
    return a > b ? a : b;
}

static int neighbor_cmp(const void *a, const void *b) {
    const cg_neighbor *na = (const cg_neighbor *)a;
    const cg_neighbor *nb = (const cg_neighbor *)b;

    if (na->distance < nb->distance) {
        return -1;
    }
    if (na->distance > nb->distance) {
        return 1;
    }
    if (na->node < nb->node) {
        return -1;
    }
    if (na->node > nb->node) {
        return 1;
    }
    return 0;
}

void cg_graph_spec_init_defaults(cg_graph_spec *spec) {
    if (spec == NULL) {
        return;
    }
    *spec = (cg_graph_spec){
        .mode = "threshold",
        .expansion_mode = "either",
        .top_k = 2,
        .adaptive_margin = 0.1,
        .adaptive_either_max = 0.15,
        .adaptive_ambiguity = 0.5,
        .mutual_boost = 0.2,
        .triangle_boost = 0.3,
        .contrastive_penalty = 0.0,
        .contrastive_floor = 0.05,
        .risk_rank_weight = 0.5,
        .risk_gap_weight = 0.5,
        .unilateral_penalty = 0.0,
        .rival_floor = 0.6
    };
}

void cg_refine_spec_init_defaults(cg_refine_spec *spec) {
    if (spec == NULL) {
        return;
    }
    *spec = (cg_refine_spec){
        .mode = "none",
        .confidence_threshold = 0.55,
        .bridge_ratio = 0.7
    };
}

static cg_status edge_push(cg_edge **edges, uint64_t *count, uint64_t *cap, uint64_t a, uint64_t b, double weight) {
    cg_edge *next;

    if (*count == *cap) {
        uint64_t next_cap = *cap == 0 ? 16 : *cap * 2;
        next = (cg_edge *)realloc(*edges, (size_t)next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        *edges = next;
        *cap = next_cap;
    }
    if (a < b) {
        (*edges)[*count] = (cg_edge){a, b, weight};
    } else {
        (*edges)[*count] = (cg_edge){b, a, weight};
    }
    (*count)++;
    return CG_OK;
}

static cg_status neighbor_push(cg_neighbor **items, uint64_t *count, uint64_t *cap, uint64_t node, double distance) {
    cg_neighbor *next;

    if (*count == *cap) {
        uint64_t next_cap = *cap == 0 ? 8 : *cap * 2;
        next = (cg_neighbor *)realloc(*items, (size_t)next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        *items = next;
        *cap = next_cap;
    }
    (*items)[*count] = (cg_neighbor){node, distance};
    (*count)++;
    return CG_OK;
}

static int top_contains(cg_neighbor **neighbors, uint64_t *counts, int top_k, uint64_t a, uint64_t b) {
    uint64_t i;
    uint64_t limit;

    if (neighbors == NULL || counts == NULL || a == UINT64_MAX) {
        return 0;
    }
    limit = counts[a] < (uint64_t)top_k ? counts[a] : (uint64_t)top_k;
    for (i = 0; i < limit; i++) {
        if (neighbors[a][i].node == b) {
            return 1;
        }
    }
    return 0;
}

static int top_rank(cg_neighbor **neighbors, uint64_t *counts, int top_k, uint64_t a, uint64_t b) {
    uint64_t i;
    uint64_t limit = counts[a] < (uint64_t)top_k ? counts[a] : (uint64_t)top_k;

    for (i = 0; i < limit; i++) {
        if (neighbors[a][i].node == b) {
            return (int)i + 1;
        }
    }
    return 0;
}

static double profile_margin(cg_node_profile p) {
    double m = p.best_weight - p.second_weight;

    return m < 0.0 ? 0.0 : m;
}

static double ambiguous_share(cg_node_profile *profiles, uint64_t n, double margin) {
    uint64_t active = 0;
    uint64_t ambiguous = 0;
    uint64_t i;

    for (i = 0; i < n; i++) {
        if (profiles[i].best_weight <= 0.0) {
            continue;
        }
        active++;
        if (profile_margin(profiles[i]) < margin) {
            ambiguous++;
        }
    }
    return active == 0 ? 0.0 : (double)ambiguous / (double)active;
}

static int sponsors_unilateral(uint64_t src, uint64_t dst, cg_neighbor **neighbors, uint64_t *counts, int top_k, cg_node_profile *profiles, double margin) {
    if (!top_contains(neighbors, counts, top_k, src, dst) || top_contains(neighbors, counts, top_k, dst, src)) {
        return 0;
    }
    if (top_rank(neighbors, counts, top_k, src, dst) != 1) {
        return 0;
    }
    return profile_margin(profiles[src]) >= margin;
}

static int adaptive_allows_unilateral(const cg_distance_pair *p, cg_neighbor **neighbors, uint64_t *counts, int top_k, cg_node_profile *profiles, double margin) {
    return sponsors_unilateral(p->a, p->b, neighbors, counts, top_k, profiles, margin) ||
        sponsors_unilateral(p->b, p->a, neighbors, counts, top_k, profiles, margin);
}

static double adj_get(double *adj, uint64_t n, uint64_t a, uint64_t b) {
    return adj[a * n + b];
}

static void adj_set(double *adj, uint64_t n, uint64_t a, uint64_t b, double weight) {
    adj[a * n + b] = weight;
    adj[b * n + a] = weight;
}

static double triangle_support(double *adj, uint64_t n, uint64_t a, uint64_t b) {
    uint64_t ca = 0;
    uint64_t cb = 0;
    uint64_t inter = 0;
    uint64_t k;
    uint64_t denom;

    for (k = 0; k < n; k++) {
        if (adj_get(adj, n, a, k) > 0.0) {
            ca++;
        }
        if (adj_get(adj, n, b, k) > 0.0) {
            cb++;
        }
    }
    if (ca <= 1 || cb <= 1) {
        return 0.0;
    }
    for (k = 0; k < n; k++) {
        if (k == a || k == b) {
            continue;
        }
        if (adj_get(adj, n, a, k) > 0.0 && adj_get(adj, n, b, k) > 0.0) {
            inter++;
        }
    }
    denom = (ca - 1) < (cb - 1) ? (ca - 1) : (cb - 1);
    return denom == 0 ? 0.0 : (double)inter / (double)denom;
}

static double endpoint_risk(uint64_t src, uint64_t dst, double base, cg_neighbor **neighbors, uint64_t *counts, int top_k, cg_node_profile *profiles, double floor, double *gap_risk_out) {
    uint64_t rank = 1;
    uint64_t i;
    double rank_risk = 0.0;
    double gap_risk = 0.0;
    double gap;

    for (i = 0; i < counts[src]; i++) {
        if (neighbors[src][i].node == dst) {
            rank = i + 1;
            break;
        }
    }
    if (rank > 1) {
        uint64_t denom = top_k > 1 ? (uint64_t)top_k : 1;
        uint64_t r = rank - 1;
        if (r > (uint64_t)top_k) {
            r = (uint64_t)top_k;
        }
        rank_risk = (double)r / (double)denom;
    }
    gap = profiles[src].best_weight - base - floor;
    if (gap > 0.0) {
        gap_risk = gap / max_double(1e-9, 1.0 - floor);
        if (gap_risk > 1.0) {
            gap_risk = 1.0;
        }
    }
    *gap_risk_out = gap_risk;
    return rank_risk;
}

static double calibrated_edge_risk(uint64_t a, uint64_t b, double base, cg_neighbor **neighbors, uint64_t *counts, int top_k, cg_node_profile *profiles, double floor, double rank_w, double gap_w) {
    double gap_a = 0.0;
    double gap_b = 0.0;
    double rank_a = endpoint_risk(a, b, base, neighbors, counts, top_k, profiles, floor, &gap_a);
    double rank_b = endpoint_risk(b, a, base, neighbors, counts, top_k, profiles, floor, &gap_b);
    double total_w = rank_w + gap_w;
    double risk;

    if (total_w <= 0.0) {
        return 0.0;
    }
    risk = (rank_w * 0.5 * (rank_a + rank_b) + gap_w * 0.5 * (gap_a + gap_b)) / total_w;
    if (risk < 0.0) {
        return 0.0;
    }
    if (risk > 1.0) {
        return 1.0;
    }
    return risk;
}

static double rival_support(double best_mutual, double best_any, double current) {
    if (best_mutual > current) {
        return best_mutual;
    }
    if (best_any > current) {
        return best_any;
    }
    return 0.0;
}

cg_status cg_build_graph_edges(
    uint64_t node_count,
    const cg_distance_pair *pairs,
    uint64_t pair_count,
    double threshold,
    const cg_graph_spec *spec,
    cg_edge **edges_out,
    uint64_t *edge_count_out
) {
    cg_graph_spec defaults;
    const cg_graph_spec *s;
    cg_edge *edges = NULL;
    uint64_t edge_count = 0;
    uint64_t edge_cap = 0;
    uint64_t i;

    if (edges_out == NULL || edge_count_out == NULL || (pairs == NULL && pair_count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *edges_out = NULL;
    *edge_count_out = 0;
    cg_graph_spec_init_defaults(&defaults);
    s = spec == NULL ? &defaults : spec;
    if (s->mode == NULL || strcmp(s->mode, "threshold") == 0 || strcmp(s->mode, "") == 0) {
        for (i = 0; i < pair_count; i++) {
            double w;
            if (pairs[i].distance >= threshold) {
                continue;
            }
            w = similarity(pairs[i].distance);
            if (w <= 0.0) {
                continue;
            }
            if (edge_push(&edges, &edge_count, &edge_cap, pairs[i].a, pairs[i].b, w) != CG_OK) {
                cg_edges_free(edges);
                return CG_ERR_OOM;
            }
        }
        *edges_out = edges;
        *edge_count_out = edge_count;
        return CG_OK;
    }
    {
        int top_k = s->top_k <= 0 ? 2 : s->top_k;
        const char *policy = s->expansion_mode == NULL || s->expansion_mode[0] == '\0' ? "either" : s->expansion_mode;
        double mutual_boost = s->mutual_boost < 0.0 ? 0.0 : s->mutual_boost;
        double triangle_boost = s->triangle_boost < 0.0 ? 0.0 : s->triangle_boost;
        double contrastive_penalty = s->contrastive_penalty;
        double contrastive_floor = s->contrastive_floor < 0.0 ? 0.0 : s->contrastive_floor;
        double rank_w = s->risk_rank_weight < 0.0 ? 0.0 : s->risk_rank_weight;
        double gap_w = s->risk_gap_weight < 0.0 ? 0.0 : s->risk_gap_weight;
        double unilateral_penalty = s->unilateral_penalty;
        double rival_floor = s->rival_floor <= 0.0 ? 0.6 : s->rival_floor;
        double adaptive_margin = s->adaptive_margin <= 0.0 ? 0.1 : s->adaptive_margin;
        double adaptive_either_max = s->adaptive_either_max < 0.0 ? 0.0 : s->adaptive_either_max;
        double adaptive_ambiguity = s->adaptive_ambiguity <= 0.0 ? 0.5 : s->adaptive_ambiguity;
        cg_neighbor **neighbors = NULL;
        uint64_t *neighbor_counts = NULL;
        uint64_t *neighbor_caps = NULL;
        cg_node_profile *profiles = NULL;
        double *candidate_adj = NULL;
        double *best_mutual = NULL;
        double *best_any = NULL;
        cg_status status = CG_OK;

        if (contrastive_penalty < 0.0) {
            contrastive_penalty = 0.0;
        }
        if (contrastive_penalty > 1.0) {
            contrastive_penalty = 1.0;
        }
        if (rank_w + gap_w == 0.0) {
            rank_w = 0.5;
            gap_w = 0.5;
        }
        if (unilateral_penalty < 0.0) {
            unilateral_penalty = 0.0;
        }
        if (unilateral_penalty > 1.0) {
            unilateral_penalty = 1.0;
        }
        if (adaptive_either_max > adaptive_ambiguity) {
            adaptive_either_max = adaptive_ambiguity;
        }
        neighbors = (cg_neighbor **)calloc((size_t)node_count, sizeof(*neighbors));
        neighbor_counts = (uint64_t *)calloc((size_t)node_count, sizeof(*neighbor_counts));
        neighbor_caps = (uint64_t *)calloc((size_t)node_count, sizeof(*neighbor_caps));
        profiles = (cg_node_profile *)calloc((size_t)node_count, sizeof(*profiles));
        candidate_adj = (double *)calloc((size_t)(node_count * node_count), sizeof(*candidate_adj));
        best_mutual = (double *)calloc((size_t)node_count, sizeof(*best_mutual));
        best_any = (double *)calloc((size_t)node_count, sizeof(*best_any));
        if ((node_count > 0) && (neighbors == NULL || neighbor_counts == NULL || neighbor_caps == NULL ||
            profiles == NULL || candidate_adj == NULL || best_mutual == NULL || best_any == NULL)) {
            status = CG_ERR_OOM;
            goto support_done;
        }
        for (i = 0; i < pair_count; i++) {
            if (pairs[i].a >= node_count || pairs[i].b >= node_count) {
                continue;
            }
            status = neighbor_push(&neighbors[pairs[i].a], &neighbor_counts[pairs[i].a], &neighbor_caps[pairs[i].a], pairs[i].b, pairs[i].distance);
            if (status == CG_OK) {
                status = neighbor_push(&neighbors[pairs[i].b], &neighbor_counts[pairs[i].b], &neighbor_caps[pairs[i].b], pairs[i].a, pairs[i].distance);
            }
            if (status != CG_OK) {
                goto support_done;
            }
        }
        for (i = 0; i < node_count; i++) {
            uint64_t j;
            qsort(neighbors[i], (size_t)neighbor_counts[i], sizeof(*neighbors[i]), neighbor_cmp);
            if (neighbor_counts[i] > 0) {
                profiles[i].best_weight = similarity(neighbors[i][0].distance);
                if (neighbor_counts[i] > 1) {
                    profiles[i].second_weight = similarity(neighbors[i][1].distance);
                }
            }
            for (j = 0; j < neighbor_counts[i] && j < (uint64_t)top_k; j++) {
                double w = similarity(neighbors[i][j].distance);
                if (w > best_any[i]) {
                    best_any[i] = w;
                }
            }
        }
        if (strcmp(policy, "adaptive") == 0) {
            double amb = ambiguous_share(profiles, node_count, adaptive_margin);
            if (amb <= adaptive_either_max) {
                policy = "either";
            } else if (amb >= adaptive_ambiguity) {
                policy = "mutual";
            }
        }
        for (i = 0; i < pair_count; i++) {
            int mutual;
            double w;
            if (pairs[i].a >= node_count || pairs[i].b >= node_count) {
                continue;
            }
            mutual = top_contains(neighbors, neighbor_counts, top_k, pairs[i].a, pairs[i].b) &&
                top_contains(neighbors, neighbor_counts, top_k, pairs[i].b, pairs[i].a);
            if (mutual) {
                w = similarity(pairs[i].distance);
                if (w > best_mutual[pairs[i].a]) {
                    best_mutual[pairs[i].a] = w;
                }
                if (w > best_mutual[pairs[i].b]) {
                    best_mutual[pairs[i].b] = w;
                }
            }
        }
        for (i = 0; i < pair_count; i++) {
            int mutual;
            int in_top;
            double w;
            if (pairs[i].a >= node_count || pairs[i].b >= node_count) {
                continue;
            }
            mutual = top_contains(neighbors, neighbor_counts, top_k, pairs[i].a, pairs[i].b) &&
                top_contains(neighbors, neighbor_counts, top_k, pairs[i].b, pairs[i].a);
            in_top = mutual;
            if (strcmp(policy, "mutual") == 0) {
                in_top = mutual;
            } else if (strcmp(policy, "adaptive") == 0) {
                in_top = mutual || adaptive_allows_unilateral(&pairs[i], neighbors, neighbor_counts, top_k, profiles, adaptive_margin);
            } else {
                in_top = top_contains(neighbors, neighbor_counts, top_k, pairs[i].a, pairs[i].b) ||
                    top_contains(neighbors, neighbor_counts, top_k, pairs[i].b, pairs[i].a);
            }
            if (pairs[i].distance >= threshold && !in_top) {
                continue;
            }
            w = similarity(pairs[i].distance);
            if (w > 0.0) {
                adj_set(candidate_adj, node_count, pairs[i].a, pairs[i].b, w);
            }
        }
        for (i = 0; i < pair_count; i++) {
            double base;
            double weight;
            double tri;
            int mutual;
            int expanded_only;
            if (pairs[i].a >= node_count || pairs[i].b >= node_count) {
                continue;
            }
            base = adj_get(candidate_adj, node_count, pairs[i].a, pairs[i].b);
            if (base <= 0.0) {
                continue;
            }
            mutual = top_contains(neighbors, neighbor_counts, top_k, pairs[i].a, pairs[i].b) &&
                top_contains(neighbors, neighbor_counts, top_k, pairs[i].b, pairs[i].a);
            expanded_only = pairs[i].distance >= threshold;
            weight = base;
            if (mutual) {
                weight += mutual_boost * (1.0 - base);
            }
            tri = triangle_support(candidate_adj, node_count, pairs[i].a, pairs[i].b);
            if (tri > 0.0) {
                weight += triangle_boost * tri * (1.0 - weight);
            }
            if (contrastive_penalty > 0.0) {
                double risk = calibrated_edge_risk(pairs[i].a, pairs[i].b, base, neighbors, neighbor_counts, top_k, profiles, contrastive_floor, rank_w, gap_w);
                if (risk > 0.0) {
                    weight *= 1.0 - contrastive_penalty * risk;
                }
            }
            if (expanded_only && !mutual && unilateral_penalty > 0.0) {
                double rival_a = rival_support(best_mutual[pairs[i].a], best_any[pairs[i].a], base);
                double rival_b = rival_support(best_mutual[pairs[i].b], best_any[pairs[i].b], base);
                double edge_rival = min_double(rival_a, rival_b);
                if (edge_rival > rival_floor) {
                    double strength = (edge_rival - rival_floor) / (1.0 - rival_floor);
                    if (strength < 0.0) {
                        strength = 0.0;
                    }
                    if (strength > 1.0) {
                        strength = 1.0;
                    }
                    weight *= 1.0 - unilateral_penalty * strength;
                }
            }
            if (weight > 1.0) {
                weight = 1.0;
            }
            status = edge_push(&edges, &edge_count, &edge_cap, pairs[i].a, pairs[i].b, weight);
            if (status != CG_OK) {
                goto support_done;
            }
        }
support_done:
        if (neighbors != NULL) {
            for (i = 0; i < node_count; i++) {
                free(neighbors[i]);
            }
        }
        free(neighbors);
        free(neighbor_counts);
        free(neighbor_caps);
        free(profiles);
        free(candidate_adj);
        free(best_mutual);
        free(best_any);
        if (status != CG_OK) {
            cg_edges_free(edges);
            return status;
        }
        *edges_out = edges;
        *edge_count_out = edge_count;
        return CG_OK;
    }
}

void cg_edges_free(cg_edge *edges) {
    free(edges);
}

static cg_status graph_add_adj(cg_graph *graph, uint64_t a, uint64_t b, double weight) {
    cg_adj_entry *next;

    if (graph->adj_count[a] == graph->adj_cap[a]) {
        uint64_t next_cap = graph->adj_cap[a] == 0 ? 4 : graph->adj_cap[a] * 2;
        next = (cg_adj_entry *)realloc(graph->adj[a], (size_t)next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        graph->adj[a] = next;
        graph->adj_cap[a] = next_cap;
    }
    graph->adj[a][graph->adj_count[a]++] = (cg_adj_entry){b, weight};
    return CG_OK;
}

cg_status cg_graph_new(uint64_t node_count, const cg_edge *edges, uint64_t edge_count, cg_graph **out) {
    cg_graph *graph;
    uint64_t i;

    if (out == NULL || (edges == NULL && edge_count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    graph = (cg_graph *)calloc(1, sizeof(*graph));
    if (graph == NULL) {
        return CG_ERR_OOM;
    }
    graph->node_count = node_count;
    graph->adj = (cg_adj_entry **)calloc((size_t)node_count, sizeof(*graph->adj));
    graph->adj_count = (uint64_t *)calloc((size_t)node_count, sizeof(*graph->adj_count));
    graph->adj_cap = (uint64_t *)calloc((size_t)node_count, sizeof(*graph->adj_cap));
    graph->strength = (double *)calloc((size_t)node_count, sizeof(*graph->strength));
    if (node_count > 0 && (graph->adj == NULL || graph->adj_count == NULL || graph->adj_cap == NULL || graph->strength == NULL)) {
        cg_graph_free(graph);
        return CG_ERR_OOM;
    }
    for (i = 0; i < edge_count; i++) {
        if (edges[i].a == edges[i].b || edges[i].weight <= 0.0 || edges[i].a >= node_count || edges[i].b >= node_count) {
            continue;
        }
        if (graph_add_adj(graph, edges[i].a, edges[i].b, edges[i].weight) != CG_OK ||
            graph_add_adj(graph, edges[i].b, edges[i].a, edges[i].weight) != CG_OK) {
            cg_graph_free(graph);
            return CG_ERR_OOM;
        }
        graph->strength[edges[i].a] += edges[i].weight;
        graph->strength[edges[i].b] += edges[i].weight;
        graph->w += 2.0 * edges[i].weight;
    }
    *out = graph;
    return CG_OK;
}

void cg_graph_free(cg_graph *graph) {
    uint64_t i;

    if (graph == NULL) {
        return;
    }
    if (graph->adj != NULL) {
        for (i = 0; i < graph->node_count; i++) {
            free(graph->adj[i]);
        }
    }
    free(graph->adj);
    free(graph->adj_count);
    free(graph->adj_cap);
    free(graph->strength);
    free(graph);
}

uint64_t cg_graph_node_count(const cg_graph *graph) {
    return graph == NULL ? 0 : graph->node_count;
}

uint64_t cg_graph_neighbor_count(const cg_graph *graph, uint64_t node) {
    if (graph == NULL || node >= graph->node_count) {
        return 0;
    }
    return graph->adj_count[node];
}

static double plogp(double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    return x * (log(x) / log(2.0));
}

cg_status cg_partition_from_modules(const uint64_t *modules, uint64_t count, cg_partition **out) {
    cg_partition *part;

    if (out == NULL || (modules == NULL && count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    part = (cg_partition *)calloc(1, sizeof(*part));
    if (part == NULL) {
        return CG_ERR_OOM;
    }
    part->modules = (uint64_t *)calloc((size_t)count, sizeof(*part->modules));
    if (count > 0 && part->modules == NULL) {
        free(part);
        return CG_ERR_OOM;
    }
    memcpy(part->modules, modules, (size_t)count * sizeof(*modules));
    part->count = count;
    *out = part;
    return CG_OK;
}

void cg_partition_free(cg_partition *partition) {
    if (partition == NULL) {
        return;
    }
    free(partition->modules);
    free(partition);
}

uint64_t cg_partition_count(const cg_partition *partition) {
    return partition == NULL ? 0 : partition->count;
}

uint64_t cg_partition_module_at(const cg_partition *partition, uint64_t node) {
    if (partition == NULL || node >= partition->count) {
        return UINT64_MAX;
    }
    return partition->modules[node];
}

double cg_graph_map_equation(const cg_graph *graph, const uint64_t *modules, uint64_t module_count) {
    uint64_t *unique = NULL;
    double *mod_p = NULL;
    double *mod_q = NULL;
    uint64_t unique_count = 0;
    uint64_t i;
    double q_sum = 0.0;
    double hq = 0.0;
    double hmods = 0.0;

    if (graph == NULL || modules == NULL || module_count != graph->node_count || graph->w == 0.0) {
        return 0.0;
    }
    unique = (uint64_t *)calloc((size_t)module_count, sizeof(*unique));
    mod_p = (double *)calloc((size_t)module_count, sizeof(*mod_p));
    mod_q = (double *)calloc((size_t)module_count, sizeof(*mod_q));
    if (unique == NULL || mod_p == NULL || mod_q == NULL) {
        free(unique);
        free(mod_p);
        free(mod_q);
        return 0.0;
    }
    for (i = 0; i < module_count; i++) {
        uint64_t m;
        int found = 0;
        for (m = 0; m < unique_count; m++) {
            if (unique[m] == modules[i]) {
                found = 1;
                break;
            }
        }
        if (!found) {
            m = unique_count;
            unique[unique_count++] = modules[i];
        }
        mod_p[m] += graph->strength[i] / graph->w;
        {
            uint64_t j;
            for (j = 0; j < graph->adj_count[i]; j++) {
                if (modules[graph->adj[i][j].target] != modules[i]) {
                    mod_q[m] += graph->adj[i][j].weight / graph->w;
                }
            }
        }
    }
    for (i = 0; i < unique_count; i++) {
        q_sum += mod_q[i];
    }
    if (q_sum > 0.0) {
        for (i = 0; i < unique_count; i++) {
            hq += plogp(mod_q[i] / q_sum);
        }
        hq = -hq;
    }
    for (i = 0; i < unique_count; i++) {
        uint64_t v;
        double usage = mod_q[i] + mod_p[i];
        double hi = 0.0;
        if (usage <= 0.0) {
            continue;
        }
        hi += plogp(mod_q[i] / usage);
        for (v = 0; v < graph->node_count; v++) {
            if (modules[v] == unique[i]) {
                hi += plogp((graph->strength[v] / graph->w) / usage);
            }
        }
        hmods += -usage * hi;
    }
    free(unique);
    free(mod_p);
    free(mod_q);
    return q_sum * hq + hmods;
}

cg_status cg_cluster_infomap(const cg_graph *graph, uint64_t seed, cg_partition **out) {
    cg_partition *part;
    uint64_t i;
    (void)seed;

    if (graph == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    part = (cg_partition *)calloc(1, sizeof(*part));
    if (part == NULL) {
        return CG_ERR_OOM;
    }
    part->modules = (uint64_t *)calloc((size_t)graph->node_count, sizeof(*part->modules));
    if (graph->node_count > 0 && part->modules == NULL) {
        free(part);
        return CG_ERR_OOM;
    }
    part->count = graph->node_count;
    for (i = 0; i < graph->node_count; i++) {
        part->modules[i] = i;
    }
    if (graph->w > 0.0) {
        int improved = 1;
        while (improved) {
            improved = 0;
            for (i = 0; i < graph->node_count; i++) {
                uint64_t *cands = NULL;
                uint64_t cand_count = 0;
                uint64_t cand_cap = 0;
                uint64_t j;
                uint64_t orig = part->modules[i];
                uint64_t best_mod = orig;
                double best_l = cg_graph_map_equation(graph, part->modules, part->count);

                cands = (uint64_t *)calloc((size_t)(graph->adj_count[i] + 1), sizeof(*cands));
                if (graph->adj_count[i] + 1 > 0 && cands == NULL) {
                    cg_partition_free(part);
                    return CG_ERR_OOM;
                }
                cands[cand_count++] = orig;
                cand_cap = graph->adj_count[i] + 1;
                for (j = 0; j < graph->adj_count[i]; j++) {
                    uint64_t mod = part->modules[graph->adj[i][j].target];
                    uint64_t k;
                    int found = 0;
                    for (k = 0; k < cand_count; k++) {
                        if (cands[k] == mod) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found && cand_count < cand_cap) {
                        cands[cand_count++] = mod;
                    }
                }
                for (j = 0; j < cand_count; j++) {
                    double l;
                    if (cands[j] == orig) {
                        continue;
                    }
                    part->modules[i] = cands[j];
                    l = cg_graph_map_equation(graph, part->modules, part->count);
                    if (l < best_l - 1e-12) {
                        best_l = l;
                        best_mod = cands[j];
                    }
                }
                part->modules[i] = best_mod;
                if (best_mod != orig) {
                    improved = 1;
                }
                free(cands);
            }
        }
    }
    {
        uint64_t *seen = (uint64_t *)calloc((size_t)part->count, sizeof(*seen));
        uint64_t seen_count = 0;
        if (part->count > 0 && seen == NULL) {
            cg_partition_free(part);
            return CG_ERR_OOM;
        }
        for (i = 0; i < part->count; i++) {
            uint64_t j;
            int found = 0;
            for (j = 0; j < seen_count; j++) {
                if (seen[j] == part->modules[i]) {
                    part->modules[i] = j;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                seen[seen_count] = part->modules[i];
                part->modules[i] = seen_count++;
            }
        }
        free(seen);
    }
    *out = part;
    return CG_OK;
}

static cg_support_profile node_profile(const cg_graph *graph, const cg_partition *part, uint64_t node) {
    cg_support_profile profile;
    double *alt_total;
    uint64_t *alt_count;
    uint64_t j;

    memset(&profile, 0, sizeof(profile));
    profile.best_alt_mod = UINT64_MAX;
    if (graph == NULL || part == NULL || node >= graph->node_count || node >= part->count) {
        return profile;
    }
    alt_total = (double *)calloc((size_t)part->count, sizeof(*alt_total));
    alt_count = (uint64_t *)calloc((size_t)part->count, sizeof(*alt_count));
    if ((part->count > 0) && (alt_total == NULL || alt_count == NULL)) {
        free(alt_total);
        free(alt_count);
        return profile;
    }
    for (j = 0; j < graph->adj_count[node]; j++) {
        uint64_t target = graph->adj[node][j].target;
        double weight = graph->adj[node][j].weight;
        uint64_t mod = part->modules[target];
        if (mod == part->modules[node]) {
            profile.in_total += weight;
            profile.in_count++;
        } else if (mod < part->count) {
            alt_total[mod] += weight;
            alt_count[mod]++;
        }
    }
    if (profile.in_count > 0) {
        profile.in_mean = profile.in_total / (double)profile.in_count;
    }
    for (j = 0; j < part->count; j++) {
        if (alt_total[j] > profile.best_alt_total) {
            profile.best_alt_total = alt_total[j];
            profile.best_alt_mod = j;
            profile.best_alt_count = alt_count[j];
            profile.has_alt = 1;
        }
    }
    if (profile.best_alt_count > 0) {
        profile.best_alt_mean = profile.best_alt_total / (double)profile.best_alt_count;
    }
    free(alt_total);
    free(alt_count);
    return profile;
}

double cg_confidence_from_support(double in_mean, double alt_weight) {
    if (in_mean <= 0.0) {
        if (alt_weight <= 0.0) {
            return 1.0;
        }
        return 1.0 - alt_weight < 0.0 ? 0.0 : 1.0 - alt_weight;
    }
    if (alt_weight <= 0.0) {
        return 1.0;
    }
    return in_mean / (in_mean + alt_weight);
}

static uint64_t rng_next(uint64_t *state) {
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state;
}

static double rng_uniform(uint64_t *state) {
    return ((double)(rng_next(state) >> 11)) * (1.0 / 9007199254740992.0);
}

static double rng_normal(uint64_t *state) {
    double u1 = rng_uniform(state);
    double u2 = rng_uniform(state);
    if (u1 < 1e-12) {
        u1 = 1e-12;
    }
    return sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
}

static int double_cmp_graph(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;

    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

static double quantile_sorted_graph(const double *xs, uint64_t count, double q) {
    double pos;
    uint64_t lo;
    uint64_t hi;
    double frac;

    if (count == 0) {
        return 0.0;
    }
    if (q <= 0.0) {
        return xs[0];
    }
    if (q >= 1.0) {
        return xs[count - 1];
    }
    pos = q * (double)(count - 1);
    lo = (uint64_t)pos;
    hi = lo + 1;
    if (hi >= count) {
        return xs[lo];
    }
    frac = pos - (double)lo;
    return xs[lo] * (1.0 - frac) + xs[hi] * frac;
}

static double jaccard_excluding(const cg_partition *a, const cg_partition *b, uint64_t node) {
    uint64_t i;
    uint64_t inter = 0;
    uint64_t uni = 0;

    for (i = 0; i < a->count; i++) {
        int in_a;
        int in_b;
        if (i == node) {
            continue;
        }
        in_a = a->modules[i] == a->modules[node];
        in_b = b->modules[i] == b->modules[node];
        if (in_a || in_b) {
            uni++;
        }
        if (in_a && in_b) {
            inter++;
        }
    }
    return uni == 0 ? 1.0 : (double)inter / (double)uni;
}

cg_status cg_bootstrap_confidences(
    uint64_t node_count,
    const cg_distance_pair *pairs,
    uint64_t pair_count,
    const cg_partition *canonical,
    double threshold,
    int runs,
    double sigma,
    uint64_t seed,
    const cg_graph_spec *graph_spec,
    cg_bootstrap_summary **out
) {
    cg_bootstrap_summary *summary;
    double **samples = NULL;
    cg_distance_pair *perturbed = NULL;
    uint64_t state = seed == 0 ? 1 : seed;
    int r;
    uint64_t i;

    if (out == NULL || canonical == NULL || canonical->count != node_count || runs < 0 || (pairs == NULL && pair_count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    summary = (cg_bootstrap_summary *)calloc(1, sizeof(*summary));
    if (summary == NULL) {
        return CG_ERR_OOM;
    }
    summary->count = node_count;
    summary->mean = (double *)calloc((size_t)node_count, sizeof(*summary->mean));
    summary->low = (double *)calloc((size_t)node_count, sizeof(*summary->low));
    summary->high = (double *)calloc((size_t)node_count, sizeof(*summary->high));
    samples = (double **)calloc((size_t)node_count, sizeof(*samples));
    perturbed = (cg_distance_pair *)calloc((size_t)pair_count, sizeof(*perturbed));
    if (node_count > 0 && (summary->mean == NULL || summary->low == NULL || summary->high == NULL || samples == NULL)) {
        cg_bootstrap_summary_free(summary);
        free(samples);
        free(perturbed);
        return CG_ERR_OOM;
    }
    if (pair_count > 0 && perturbed == NULL) {
        cg_bootstrap_summary_free(summary);
        free(samples);
        return CG_ERR_OOM;
    }
    for (i = 0; i < node_count; i++) {
        samples[i] = (double *)calloc((size_t)(runs > 0 ? runs : 1), sizeof(*samples[i]));
        if (samples[i] == NULL) {
            uint64_t j;
            for (j = 0; j < i; j++) {
                free(samples[j]);
            }
            cg_bootstrap_summary_free(summary);
            free(samples);
            free(perturbed);
            return CG_ERR_OOM;
        }
    }
    if (runs <= 0 || node_count == 0) {
        for (i = 0; i < node_count; i++) {
            free(samples[i]);
        }
        free(samples);
        free(perturbed);
        *out = summary;
        return CG_OK;
    }
    for (r = 0; r < runs; r++) {
        cg_edge *edges = NULL;
        uint64_t edge_count = 0;
        cg_graph *graph = NULL;
        cg_partition *part = NULL;
        cg_status status;
        for (i = 0; i < pair_count; i++) {
            double d = pairs[i].distance + rng_normal(&state) * sigma;
            if (d < 0.0) {
                d = 0.0;
            }
            if (d > 1.0) {
                d = 1.0;
            }
            perturbed[i] = pairs[i];
            perturbed[i].distance = d;
        }
        status = cg_build_graph_edges(node_count, perturbed, pair_count, threshold, graph_spec, &edges, &edge_count);
        if (status == CG_OK) {
            status = cg_graph_new(node_count, edges, edge_count, &graph);
        }
        if (status == CG_OK) {
            status = cg_cluster_infomap(graph, seed + (uint64_t)r + 1, &part);
        }
        if (status != CG_OK) {
            cg_edges_free(edges);
            cg_graph_free(graph);
            cg_partition_free(part);
            for (i = 0; i < node_count; i++) {
                free(samples[i]);
            }
            cg_bootstrap_summary_free(summary);
            free(samples);
            free(perturbed);
            return status;
        }
        for (i = 0; i < node_count; i++) {
            double score = jaccard_excluding(canonical, part, i);
            samples[i][r] = score;
            summary->mean[i] += score;
        }
        cg_edges_free(edges);
        cg_graph_free(graph);
        cg_partition_free(part);
    }
    for (i = 0; i < node_count; i++) {
        summary->mean[i] /= (double)runs;
        qsort(samples[i], (size_t)runs, sizeof(*samples[i]), double_cmp_graph);
        summary->low[i] = quantile_sorted_graph(samples[i], (uint64_t)runs, 0.05);
        summary->high[i] = quantile_sorted_graph(samples[i], (uint64_t)runs, 0.95);
        free(samples[i]);
    }
    free(samples);
    free(perturbed);
    *out = summary;
    return CG_OK;
}

void cg_bootstrap_summary_free(cg_bootstrap_summary *summary) {
    if (summary == NULL) {
        return;
    }
    free(summary->mean);
    free(summary->low);
    free(summary->high);
    free(summary);
}

uint64_t cg_bootstrap_summary_count(const cg_bootstrap_summary *summary) {
    return summary == NULL ? 0 : summary->count;
}

double cg_bootstrap_mean_at(const cg_bootstrap_summary *summary, uint64_t node) {
    if (summary == NULL || node >= summary->count) {
        return 0.0;
    }
    return summary->mean[node];
}

double cg_bootstrap_low_at(const cg_bootstrap_summary *summary, uint64_t node) {
    if (summary == NULL || node >= summary->count) {
        return 0.0;
    }
    return summary->low[node];
}

double cg_bootstrap_high_at(const cg_bootstrap_summary *summary, uint64_t node) {
    if (summary == NULL || node >= summary->count) {
        return 0.0;
    }
    return summary->high[node];
}

cg_status cg_diagnostics_new(
    const cg_graph *graph,
    const cg_partition *partition,
    cg_diagnostics **out
) {
    cg_diagnostics *diag;
    uint64_t *cluster_size;
    uint64_t i;

    if (graph == NULL || partition == NULL || out == NULL || graph->node_count != partition->count) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    diag = (cg_diagnostics *)calloc(1, sizeof(*diag));
    if (diag == NULL) {
        return CG_ERR_OOM;
    }
    diag->items = (cg_diagnostic *)calloc((size_t)partition->count, sizeof(*diag->items));
    cluster_size = (uint64_t *)calloc((size_t)partition->count, sizeof(*cluster_size));
    if (partition->count > 0 && (diag->items == NULL || cluster_size == NULL)) {
        free(diag->items);
        free(diag);
        free(cluster_size);
        return CG_ERR_OOM;
    }
    diag->count = partition->count;
    for (i = 0; i < partition->count; i++) {
        if (partition->modules[i] < partition->count) {
            cluster_size[partition->modules[i]]++;
        }
    }
    for (i = 0; i < partition->count; i++) {
        cg_support_profile p = node_profile(graph, partition, i);
        diag->items[i].assigned_support = p.in_total;
        diag->items[i].alternative_support = p.best_alt_total;
        diag->items[i].support_margin = p.in_total - p.best_alt_total;
        diag->items[i].cluster_size = partition->modules[i] < partition->count ? cluster_size[partition->modules[i]] : 0;
        diag->items[i].candidate_neighbors = graph->adj_count[i];
        if (p.in_total <= 0.0) {
            diag->items[i].confidence = p.best_alt_total <= 0.0 ? 1.0 : max_double(0.0, 1.0 - p.best_alt_total);
        } else if (p.best_alt_total <= 0.0) {
            diag->items[i].confidence = 1.0;
        } else {
            diag->items[i].confidence = p.in_total / (p.in_total + p.best_alt_total);
        }
    }
    free(cluster_size);
    *out = diag;
    return CG_OK;
}

void cg_diagnostics_free(cg_diagnostics *diagnostics) {
    if (diagnostics == NULL) {
        return;
    }
    free(diagnostics->items);
    free(diagnostics);
}

uint64_t cg_diagnostics_count(const cg_diagnostics *diagnostics) {
    return diagnostics == NULL ? 0 : diagnostics->count;
}

const cg_diagnostic *cg_diagnostics_get(const cg_diagnostics *diagnostics, uint64_t node) {
    if (diagnostics == NULL || node >= diagnostics->count) {
        return NULL;
    }
    return &diagnostics->items[node];
}

static int bridge_cut_blocks(const cg_graph *graph, const cg_partition *partition, uint64_t node, double weight, const cg_refine_spec *s) {
    cg_support_profile p = node_profile(graph, partition, node);
    double conf_mean = cg_confidence_from_support(p.in_mean, p.best_alt_mean);
    if ((conf_mean >= s->confidence_threshold && p.in_mean >= s->confidence_threshold) || p.in_mean <= 0.0) {
        return 0;
    }
    return weight < p.in_mean * s->bridge_ratio;
}

cg_status cg_refine_partition(
    const cg_graph *graph,
    const cg_partition *partition,
    const cg_refine_spec *spec,
    cg_partition **out
) {
    cg_refine_spec defaults;
    const cg_refine_spec *s;
    cg_partition *refined;
    uint64_t next = 0;
    uint64_t i;

    if (graph == NULL || partition == NULL || out == NULL || graph->node_count != partition->count) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    cg_refine_spec_init_defaults(&defaults);
    s = spec == NULL ? &defaults : spec;
    if (s->mode == NULL || strcmp(s->mode, "none") == 0 || strcmp(s->mode, "") == 0) {
        return cg_partition_from_modules(partition->modules, partition->count, out);
    }
    refined = (cg_partition *)calloc(1, sizeof(*refined));
    if (refined == NULL) {
        return CG_ERR_OOM;
    }
    refined->modules = (uint64_t *)calloc((size_t)partition->count, sizeof(*refined->modules));
    if (partition->count > 0 && refined->modules == NULL) {
        free(refined);
        return CG_ERR_OOM;
    }
    refined->count = partition->count;
    for (i = 0; i < partition->count; i++) {
        refined->modules[i] = UINT64_MAX;
    }
    for (i = 0; i < partition->count; i++) {
        uint64_t *stack;
        uint64_t stack_len = 0;
        if (refined->modules[i] != UINT64_MAX) {
            continue;
        }
        stack = (uint64_t *)calloc((size_t)partition->count, sizeof(*stack));
        if (partition->count > 0 && stack == NULL) {
            cg_partition_free(refined);
            return CG_ERR_OOM;
        }
        stack[stack_len++] = i;
        refined->modules[i] = next;
        while (stack_len > 0) {
            uint64_t v = stack[--stack_len];
            uint64_t j;
            for (j = 0; j < graph->adj_count[v]; j++) {
                uint64_t u = graph->adj[v][j].target;
                double weight = graph->adj[v][j].weight;
                int blocked = 0;
                if (partition->modules[u] != partition->modules[v] || refined->modules[u] != UINT64_MAX) {
                    continue;
                }
                if (strcmp(s->mode, "bridge-cut") == 0) {
                    blocked = bridge_cut_blocks(graph, partition, v, weight, s) || bridge_cut_blocks(graph, partition, u, weight, s);
                }
                if (!blocked) {
                    refined->modules[u] = next;
                    stack[stack_len++] = u;
                }
            }
        }
        free(stack);
        next++;
    }
    *out = refined;
    return CG_OK;
}

double cg_otsu_threshold(const double *values, uint64_t count, double lo, double hi, int bins) {
    int *hist;
    double width;
    double sum_all = 0.0;
    double best_var = 0.0;
    int best_lo = -1;
    int best_hi = -1;
    double w0 = 0.0;
    double s0 = 0.0;
    uint64_t i;
    int t;

    if (bins < 2) {
        bins = 64;
    }
    if (hi <= lo) {
        hi = lo + 1.0;
    }
    if (values == NULL || count == 0) {
        return (lo + hi) / 2.0;
    }
    hist = (int *)calloc((size_t)bins, sizeof(*hist));
    if (hist == NULL) {
        return (lo + hi) / 2.0;
    }
    width = (hi - lo) / (double)bins;
    for (i = 0; i < count; i++) {
        double v = values[i];
        int idx;
        if (v < lo) {
            v = lo;
        } else if (v > hi) {
            v = hi;
        }
        idx = (int)((v - lo) / width);
        if (idx >= bins) {
            idx = bins - 1;
        }
        hist[idx]++;
    }
    for (t = 0; t < bins; t++) {
        double center = lo + ((double)t + 0.5) * width;
        sum_all += center * (double)hist[t];
    }
    for (t = 0; t < bins - 1; t++) {
        double c = (double)hist[t];
        double w1;
        double m0;
        double m1;
        double bv;
        w0 += c;
        if (w0 == 0.0) {
            continue;
        }
        s0 += (lo + ((double)t + 0.5) * width) * c;
        w1 = (double)count - w0;
        if (w1 == 0.0) {
            break;
        }
        m0 = s0 / w0;
        m1 = (sum_all - s0) / w1;
        bv = w0 * w1 * (m0 - m1) * (m0 - m1);
        if (bv > best_var + 1e-15) {
            best_var = bv;
            best_lo = t;
            best_hi = t;
        } else if (bv >= best_var - 1e-15 && best_lo >= 0) {
            best_hi = t;
        }
    }
    free(hist);
    if (best_lo < 0) {
        return (lo + hi) / 2.0;
    }
    return lo + (((double)(best_lo + best_hi) / 2.0) + 1.0) * width;
}

double cg_clamp(double value, double min, double max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}
