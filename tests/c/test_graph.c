#include "cognator.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static int has_edge(const cg_edge *edges, uint64_t count, uint64_t a, uint64_t b, double *weight) {
    uint64_t i;

    for (i = 0; i < count; i++) {
        if ((edges[i].a == a && edges[i].b == b) || (edges[i].a == b && edges[i].b == a)) {
            if (weight != NULL) {
                *weight = edges[i].weight;
            }
            return 1;
        }
    }
    return 0;
}

int main(void) {
    cg_graph_spec spec;
    cg_refine_spec refine;
    cg_edge *edges = NULL;
    uint64_t edge_count = 0;
    cg_graph *graph = NULL;
    cg_partition *part = NULL;
    cg_partition *refined = NULL;
    cg_bootstrap_summary *boot = NULL;
    cg_diagnostics *diag = NULL;
    const cg_diagnostic *d0;
    double w_base = 0.0;
    double w_penalized = 0.0;
    double values[8] = {0.05, 0.07, 0.10, 0.12, 0.85, 0.88, 0.90, 0.92};
    double th;
    uint64_t modules[4] = {0, 0, 1, 1};

    cg_graph_spec_init_defaults(&spec);
    {
        cg_distance_pair pairs[] = {
            {0, 1, 0.20},
            {0, 2, 0.32},
            {1, 2, 0.70},
        };
        spec.mode = "support";
        spec.top_k = 1;
        spec.mutual_boost = 0.2;
        spec.triangle_boost = 0.2;
        assert(cg_build_graph_edges(3, pairs, 3, 0.30, &spec, &edges, &edge_count) == CG_OK);
        assert(edge_count == 2);
        assert(has_edge(edges, edge_count, 0, 2, NULL));
        cg_edges_free(edges);
    }

    cg_graph_spec_init_defaults(&spec);
    {
        cg_distance_pair pairs[] = {
            {0, 1, 0.23},
            {0, 2, 0.24},
            {1, 3, 0.10},
            {1, 4, 0.11},
        };
        spec.mode = "support";
        spec.expansion_mode = "either";
        spec.top_k = 1;
        assert(cg_build_graph_edges(5, pairs, 4, 0.20, &spec, &edges, &edge_count) == CG_OK);
        assert(has_edge(edges, edge_count, 0, 1, NULL));
        cg_edges_free(edges);
        spec.expansion_mode = "adaptive";
        spec.adaptive_margin = 0.1;
        spec.adaptive_either_max = 0.0;
        spec.adaptive_ambiguity = 1.0;
        assert(cg_build_graph_edges(5, pairs, 4, 0.20, &spec, &edges, &edge_count) == CG_OK);
        assert(!has_edge(edges, edge_count, 0, 1, NULL));
        cg_edges_free(edges);
    }

    cg_graph_spec_init_defaults(&spec);
    {
        cg_distance_pair pairs[] = {
            {0, 1, 0.10},
            {0, 2, 0.11},
            {1, 3, 0.10},
            {1, 4, 0.11},
            {0, 3, 0.24},
        };
        spec.mode = "support";
        spec.expansion_mode = "either";
        spec.top_k = 2;
        assert(cg_build_graph_edges(5, pairs, 5, 0.25, &spec, &edges, &edge_count) == CG_OK);
        assert(has_edge(edges, edge_count, 0, 3, &w_base));
        cg_edges_free(edges);
        spec.contrastive_penalty = 0.5;
        spec.contrastive_floor = 0.05;
        assert(cg_build_graph_edges(5, pairs, 5, 0.25, &spec, &edges, &edge_count) == CG_OK);
        assert(has_edge(edges, edge_count, 0, 3, &w_penalized));
        assert(w_penalized < w_base);
        cg_edges_free(edges);
    }

    cg_graph_spec_init_defaults(&spec);
    {
        cg_distance_pair pairs[] = {
            {0, 1, 0.20},
            {0, 2, 0.22},
            {1, 2, 0.21},
        };
        assert(cg_build_graph_edges(3, pairs, 3, 0.30, &spec, &edges, &edge_count) == CG_OK);
        assert(has_edge(edges, edge_count, 0, 1, &w_base));
        cg_edges_free(edges);
        spec.mode = "support";
        spec.top_k = 2;
        spec.mutual_boost = 0.2;
        spec.triangle_boost = 0.3;
        assert(cg_build_graph_edges(3, pairs, 3, 0.30, &spec, &edges, &edge_count) == CG_OK);
        assert(has_edge(edges, edge_count, 0, 1, &w_penalized));
        assert(w_penalized > w_base);
        cg_edges_free(edges);
    }

    {
        cg_distance_pair pairs[] = {
            {0, 1, 0.05},
            {2, 3, 0.05},
            {1, 2, 0.90},
        };
        cg_edge boot_edges[] = {
            {0, 1, 0.95},
            {2, 3, 0.95},
        };
        uint64_t canon_mods[4] = {0, 0, 1, 1};
        assert(cg_partition_from_modules(canon_mods, 4, &part) == CG_OK);
        assert(cg_bootstrap_confidences(4, pairs, 3, part, 0.30, 8, 0.01, 11, NULL, &boot) == CG_OK);
        assert(cg_bootstrap_summary_count(boot) == 4);
        assert(cg_bootstrap_mean_at(boot, 0) > 0.9);
        assert(cg_bootstrap_low_at(boot, 0) <= cg_bootstrap_mean_at(boot, 0));
        assert(cg_bootstrap_high_at(boot, 0) >= cg_bootstrap_mean_at(boot, 0));
        assert(cg_graph_new(4, boot_edges, 2, &graph) == CG_OK);
        cg_graph_free(graph);
        cg_bootstrap_summary_free(boot);
        cg_partition_free(part);
    }

    cg_graph_spec_init_defaults(&spec);
    {
        cg_distance_pair pairs[] = {
            {0, 1, 0.24},
            {0, 2, 0.90},
            {1, 3, 0.10},
            {1, 4, 0.11},
        };
        spec.mode = "support";
        spec.expansion_mode = "either";
        spec.top_k = 1;
        assert(cg_build_graph_edges(5, pairs, 4, 0.20, &spec, &edges, &edge_count) == CG_OK);
        assert(has_edge(edges, edge_count, 0, 1, NULL));
        cg_edges_free(edges);
        spec.expansion_mode = "mutual";
        assert(cg_build_graph_edges(5, pairs, 4, 0.20, &spec, &edges, &edge_count) == CG_OK);
        assert(!has_edge(edges, edge_count, 0, 1, NULL));
        cg_edges_free(edges);
    }

    cg_graph_spec_init_defaults(&spec);
    {
        cg_distance_pair pairs[] = {
            {0, 1, 0.10},
            {0, 2, 0.11},
            {2, 3, 0.10},
            {2, 4, 0.11},
            {0, 3, 0.31},
        };
        spec.mode = "support";
        spec.expansion_mode = "either";
        spec.top_k = 2;
        assert(cg_build_graph_edges(5, pairs, 5, 0.30, &spec, &edges, &edge_count) == CG_OK);
        assert(has_edge(edges, edge_count, 0, 3, &w_base));
        cg_edges_free(edges);
        spec.unilateral_penalty = 0.5;
        spec.rival_floor = 0.6;
        assert(cg_build_graph_edges(5, pairs, 5, 0.30, &spec, &edges, &edge_count) == CG_OK);
        assert(has_edge(edges, edge_count, 0, 3, &w_penalized));
        assert(w_penalized < w_base);
        cg_edges_free(edges);
    }

    th = cg_otsu_threshold(values, 8, 0.0, 1.0, 64);
    assert(th > 0.3 && th < 0.7);
    assert(cg_clamp(0.1, 0.2, 0.8) == 0.2);
    assert(cg_clamp(0.9, 0.2, 0.8) == 0.8);

    {
        cg_edge clique_edges[] = {
            {0, 1, 1.0}, {0, 2, 1.0}, {1, 2, 1.0},
            {3, 4, 1.0}, {3, 5, 1.0}, {4, 5, 1.0},
            {2, 3, 0.05},
        };
        uint64_t flat[6] = {0, 0, 0, 0, 0, 0};
        uint64_t split[6] = {0, 0, 0, 1, 1, 1};
        assert(cg_graph_new(6, clique_edges, 7, &graph) == CG_OK);
        assert(cg_graph_map_equation(graph, split, 6) < cg_graph_map_equation(graph, flat, 6));
        assert(cg_cluster_infomap(graph, 42, &part) == CG_OK);
        assert(cg_partition_module_at(part, 0) == cg_partition_module_at(part, 1));
        assert(cg_partition_module_at(part, 3) == cg_partition_module_at(part, 4));
        assert(cg_partition_module_at(part, 0) != cg_partition_module_at(part, 3));
        cg_partition_free(part);
        cg_graph_free(graph);
    }

    {
        cg_edge diag_edges[] = {
            {0, 1, 0.80},
            {0, 2, 0.30},
            {0, 3, 0.20},
            {2, 3, 0.90},
        };
        assert(cg_graph_new(4, diag_edges, 4, &graph) == CG_OK);
        assert(cg_partition_from_modules(modules, 4, &part) == CG_OK);
        assert(cg_diagnostics_new(graph, part, &diag) == CG_OK);
        d0 = cg_diagnostics_get(diag, 0);
        assert(fabs(d0->assigned_support - 0.80) < 1e-9);
        assert(fabs(d0->alternative_support - 0.50) < 1e-9);
        assert(fabs(d0->support_margin - 0.30) < 1e-9);
        assert(d0->cluster_size == 2);
        assert(d0->candidate_neighbors == 3);
        assert(d0->confidence > 0.0 && d0->confidence < 1.0);
        cg_diagnostics_free(diag);
        cg_partition_free(part);
        cg_graph_free(graph);
    }

    {
        cg_edge bridge_edges[] = {
            {0, 1, 0.95},
            {1, 2, 0.20},
            {2, 3, 0.95},
        };
        uint64_t all_one[4] = {0, 0, 0, 0};
        assert(cg_graph_new(4, bridge_edges, 3, &graph) == CG_OK);
        assert(cg_partition_from_modules(all_one, 4, &part) == CG_OK);
        cg_refine_spec_init_defaults(&refine);
        refine.mode = "bridge-cut";
        refine.confidence_threshold = 0.6;
        refine.bridge_ratio = 0.7;
        assert(cg_refine_partition(graph, part, &refine, &refined) == CG_OK);
        assert(cg_partition_module_at(refined, 0) != cg_partition_module_at(refined, 2));
        assert(cg_partition_module_at(refined, 1) != cg_partition_module_at(refined, 2));
        cg_partition_free(refined);
        cg_partition_free(part);
        cg_graph_free(graph);
    }

    {
        cg_edge bridge_edges[] = {
            {0, 1, 0.40},
            {1, 2, 0.90},
            {1, 3, 0.90},
        };
        uint64_t modules[4] = {0, 0, 1, 0};
        assert(cg_graph_new(4, bridge_edges, 3, &graph) == CG_OK);
        assert(cg_partition_from_modules(modules, 4, &part) == CG_OK);
        cg_refine_spec_init_defaults(&refine);
        refine.mode = "bridge-cut";
        refine.confidence_threshold = 0.6;
        refine.bridge_ratio = 0.7;
        assert(cg_refine_partition(graph, part, &refine, &refined) == CG_OK);
        assert(cg_partition_module_at(refined, 0) != cg_partition_module_at(refined, 1));
        assert(cg_partition_module_at(refined, 1) == cg_partition_module_at(refined, 3));
        assert(cg_partition_module_at(refined, 2) != cg_partition_module_at(refined, 1));
        cg_partition_free(refined);
        cg_partition_free(part);
        cg_graph_free(graph);
    }

    assert(cg_confidence_from_support(0.0, 0.0) == 1.0);
    assert(fabs(cg_confidence_from_support(0.0, 0.4) - 0.6) < 1e-9);
    assert(cg_build_graph_edges(0, NULL, 0, 0.5, NULL, NULL, &edge_count) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_graph_new(0, NULL, 0, NULL) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_partition_from_modules(NULL, 1, &part) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_diagnostics_get(NULL, 0) == NULL);
    assert(cg_bootstrap_confidences(0, NULL, 0, NULL, 0.3, 1, 0.01, 1, NULL, &boot) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_bootstrap_mean_at(NULL, 0) == 0.0);
    cg_bootstrap_summary_free(NULL);
    cg_graph_spec_init_defaults(NULL);
    cg_refine_spec_init_defaults(NULL);
    return 0;
}
