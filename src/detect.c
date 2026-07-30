#include "internal/align.h"
#include "internal/context.h"
#include "internal/data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cg_seq {
    char **items;
    size_t count;
} cg_seq;

typedef struct cg_lect_pair_table {
    char *a;
    char *b;
    cg_dirichlet_prior *prior;
    cg_posterior_table *table;
    int has_posterior;
} cg_lect_pair_table;

typedef struct cg_posterior_score_user {
    const cg_context *ctx;
    const cg_posterior_table *table;
    int prosody_aware;
} cg_posterior_score_user;

static cg_status posterior_score_cb(const char *source, const char *target, void *user, double *out) {
    cg_posterior_score_user *u = (cg_posterior_score_user *)user;
    return cg_posterior_score(u->ctx, u->table, source, target, u->prosody_aware, out);
}

static void seq_free(cg_seq *seq) {
    size_t i;
    if (seq == NULL) {
        return;
    }
    for (i = 0; i < seq->count; i++) {
        free(seq->items[i]);
    }
    free(seq->items);
    *seq = (cg_seq){0};
}

static cg_status seq_push(cg_seq *seq, const char *value) {
    char **next;
    next = (char **)realloc(seq->items, (seq->count + 1) * sizeof(*next));
    if (next == NULL) {
        return CG_ERR_OOM;
    }
    seq->items = next;
    seq->items[seq->count] = cg_strdup(value);
    if (seq->items[seq->count] == NULL) {
        return CG_ERR_OOM;
    }
    seq->count++;
    return CG_OK;
}

static cg_status parse_record_sequence(const cg_context *ctx, const cg_record *record, int class_reduce, cg_oov_report *oov, cg_seq *seq) {
    cg_tokens *tokens = NULL;
    uint64_t i;
    cg_status status = cg_segment_parse_with_oov(ctx, record->segments, oov, &tokens);
    if (status != CG_OK) {
        return status;
    }
    for (i = 0; i < cg_tokens_count(tokens); i++) {
        const cg_token *token = cg_tokens_get(tokens, i);
        char *reduced = NULL;
        const char *g;
        if (token == NULL || token->boundary || token->grapheme == NULL || token->grapheme[0] == '\0') {
            continue;
        }
        g = token->grapheme;
        if (class_reduce) {
            status = cg_class_reduce(g, &reduced);
            if (status != CG_OK) {
                cg_tokens_free(tokens);
                return status;
            }
            g = reduced;
        }
        status = seq_push(seq, g);
        free(reduced);
        if (status != CG_OK) {
            cg_tokens_free(tokens);
            return status;
        }
    }
    cg_tokens_free(tokens);
    return CG_OK;
}

static int str_eq(const char *a, const char *b) {
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    return strcmp(a, b) == 0;
}

static int string_less(const char *a, const char *b) {
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    return strcmp(a, b) < 0;
}

static cg_status unique_push(char ***items, size_t *count, const char *value) {
    size_t i;
    char **next;
    for (i = 0; i < *count; i++) {
        if (str_eq((*items)[i], value)) {
            return CG_OK;
        }
    }
    next = (char **)realloc(*items, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return CG_ERR_OOM;
    }
    *items = next;
    (*items)[*count] = cg_strdup(value == NULL ? "" : value);
    if ((*items)[*count] == NULL) {
        return CG_ERR_OOM;
    }
    (*count)++;
    return CG_OK;
}

static void free_string_list(char **items, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

static const cg_record *record_at(const cg_records *records, size_t index) {
    return &records->items[index];
}

static size_t find_pair_table(cg_lect_pair_table *tables, size_t count, const char *a, const char *b) {
    size_t i;
    const char *lo = string_less(a, b) ? a : b;
    const char *hi = string_less(a, b) ? b : a;
    for (i = 0; i < count; i++) {
        if (str_eq(tables[i].a, lo) && str_eq(tables[i].b, hi)) {
            return i;
        }
    }
    return SIZE_MAX;
}

static cg_status form_pair_push(cg_form_pair **pairs, size_t *count, const cg_seq *source, const cg_seq *target, const cg_record *rs, const cg_record *rt) {
    cg_form_pair *next = (cg_form_pair *)realloc(*pairs, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return CG_ERR_OOM;
    }
    *pairs = next;
    (*pairs)[*count] = (cg_form_pair){
        .source = (const char *const *)source->items,
        .source_count = (uint64_t)source->count,
        .target = (const char *const *)target->items,
        .target_count = (uint64_t)target->count,
        .concept = rs->concept,
        .source_id = rs->id,
        .target_id = rt->id
    };
    (*count)++;
    return CG_OK;
}

static cg_status build_pair_tables(const cg_context *ctx, const cg_records *records, cg_seq *seqs, const cg_options *options, cg_lect_pair_table **out, size_t *out_count) {
    char **lects = NULL;
    size_t lect_count = 0;
    cg_lect_pair_table *tables = NULL;
    size_t table_count = 0;
    size_t i;
    cg_status status = CG_OK;

    *out = NULL;
    *out_count = 0;
    for (i = 0; i < records->count; i++) {
        status = unique_push(&lects, &lect_count, record_at(records, i)->language_id);
        if (status != CG_OK) {
            goto done;
        }
    }
    for (i = 0; i < lect_count; i++) {
        size_t j;
        for (j = i + 1; j < lect_count; j++) {
            cg_form_pair *pairs = NULL;
            size_t pair_count = 0;
            size_t a;
            cg_dirichlet_prior *prior = NULL;
            cg_posterior_table *table = NULL;
            int has_posterior = 0;
            for (a = 0; a < records->count; a++) {
                size_t b;
                const cg_record *ra = record_at(records, a);
                if (!str_eq(ra->language_id, lects[i]) && !str_eq(ra->language_id, lects[j])) {
                    continue;
                }
                for (b = a + 1; b < records->count; b++) {
                    const cg_record *rb = record_at(records, b);
                    if (!str_eq(ra->concept, rb->concept)) {
                        continue;
                    }
                    if (!((str_eq(ra->language_id, lects[i]) && str_eq(rb->language_id, lects[j])) ||
                          (str_eq(ra->language_id, lects[j]) && str_eq(rb->language_id, lects[i])))) {
                        continue;
                    }
                    if (str_eq(ra->language_id, lects[i])) {
                        status = form_pair_push(&pairs, &pair_count, &seqs[a], &seqs[b], ra, rb);
                    } else {
                        status = form_pair_push(&pairs, &pair_count, &seqs[b], &seqs[a], rb, ra);
                    }
                    if (status != CG_OK) {
                        free(pairs);
                        goto done;
                    }
                }
            }
            if (options->scorer_mode != NULL && strcmp(options->scorer_mode, "posterior") == 0 && (int)pair_count >= options->min_pairs && pair_count > 0) {
                char **inventory = NULL;
                uint64_t inventory_count = 0;
                status = cg_inventory_from_pairs(pairs, (uint64_t)pair_count, &inventory, &inventory_count);
                if (status == CG_OK) {
                    status = cg_dirichlet_prior_new(ctx, options->dirichlet_concentration, options->dirichlet_sharpness, (const char *const *)inventory, inventory_count, &prior);
                }
                if (status == CG_OK) {
                    status = cg_train_posterior(ctx, lects[i], lects[j], pairs, (uint64_t)pair_count, prior, options->iterations, &table);
                }
                if (status == CG_OK && options->dirichlet_scale_mode != NULL && options->dirichlet_scale_mode[0] != '\0') {
                    status = cg_posterior_table_attach_scaling(table, options->dirichlet_scale_mode);
                }
                cg_string_array_free(inventory, inventory_count);
                if (status != CG_OK) {
                    cg_posterior_table_free(table);
                    cg_dirichlet_prior_free(prior);
                    free(pairs);
                    goto done;
                }
                has_posterior = 1;
            }
            free(pairs);
            {
                cg_lect_pair_table *next = (cg_lect_pair_table *)realloc(tables, (table_count + 1) * sizeof(*next));
                if (next == NULL) {
                    cg_posterior_table_free(table);
                    cg_dirichlet_prior_free(prior);
                    status = CG_ERR_OOM;
                    goto done;
                }
                tables = next;
                tables[table_count].a = cg_strdup(lects[i]);
                tables[table_count].b = cg_strdup(lects[j]);
                tables[table_count].prior = prior;
                tables[table_count].table = table;
                tables[table_count].has_posterior = has_posterior;
                if (tables[table_count].a == NULL || tables[table_count].b == NULL) {
                    free(tables[table_count].a);
                    free(tables[table_count].b);
                    cg_posterior_table_free(table);
                    cg_dirichlet_prior_free(prior);
                    tables[table_count] = (cg_lect_pair_table){0};
                    status = CG_ERR_OOM;
                    goto done;
                }
                prior = NULL;
                table = NULL;
                table_count++;
            }
        }
    }
done:
    free_string_list(lects, lect_count);
    if (status != CG_OK) {
        for (i = 0; i < table_count; i++) {
            free(tables[i].a);
            free(tables[i].b);
            cg_posterior_table_free(tables[i].table);
            cg_dirichlet_prior_free(tables[i].prior);
        }
        free(tables);
        return status;
    }
    *out = tables;
    *out_count = table_count;
    return CG_OK;
}

static void free_pair_tables(cg_lect_pair_table *tables, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        free(tables[i].a);
        free(tables[i].b);
        cg_posterior_table_free(tables[i].table);
        cg_dirichlet_prior_free(tables[i].prior);
    }
    free(tables);
}

static cg_status distance_for_pair(const cg_context *ctx, const cg_seq *a, const cg_seq *b, const cg_record *ra, const cg_record *rb, cg_lect_pair_table *tables, size_t table_count, const cg_options *options, double *out) {
    size_t idx = find_pair_table(tables, table_count, ra->language_id, rb->language_id);
    int reverse = string_less(rb->language_id, ra->language_id);
    const cg_seq *src = reverse ? b : a;
    const cg_seq *tgt = reverse ? a : b;
    if (idx != SIZE_MAX && tables[idx].has_posterior) {
        cg_posterior_score_user user = {ctx, tables[idx].table, 1};
        if (options->alignment_mode != NULL && strcmp(options->alignment_mode, "global") == 0) {
            return cg_internal_distance((const char *const *)src->items, src->count, (const char *const *)tgt->items, tgt->count, posterior_score_cb, &user, out);
        }
        return cg_internal_distance_combined((const char *const *)src->items, src->count, (const char *const *)tgt->items, tgt->count, posterior_score_cb, &user, out);
    }
    if (options->alignment_mode != NULL && strcmp(options->alignment_mode, "global") == 0) {
        return cg_align_distance(ctx, (const char *const *)a->items, (uint64_t)a->count, (const char *const *)b->items, (uint64_t)b->count, 1, out);
    }
    return cg_align_distance_combined(ctx, (const char *const *)a->items, (uint64_t)a->count, (const char *const *)b->items, (uint64_t)b->count, 1, out);
}

static cg_status result_push(cg_result *result, cg_sidecar_row row) {
    cg_sidecar_row *next;
    if (result->row_count == result->row_cap) {
        size_t next_cap = result->row_cap == 0 ? 32 : result->row_cap * 2;
        next = (cg_sidecar_row *)realloc(result->rows, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        result->rows = next;
        result->row_cap = next_cap;
    }
    result->rows[result->row_count++] = row;
    return CG_OK;
}

static size_t result_find_row(const cg_result *result, const char *id, char *used) {
    size_t i;
    for (i = 0; i < result->row_count; i++) {
        if (!used[i] && str_eq(result->rows[i].id, id)) {
            return i;
        }
    }
    return SIZE_MAX;
}

static cg_status result_reorder_like_records(cg_result *result, const cg_records *records) {
    cg_sidecar_row *rows;
    char *used;
    size_t i;
    if (result->row_count != records->count) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    if (result->row_count == 0) {
        return CG_OK;
    }
    rows = (cg_sidecar_row *)calloc(result->row_count, sizeof(*rows));
    used = (char *)calloc(result->row_count, sizeof(*used));
    if (rows == NULL || used == NULL) {
        free(rows);
        free(used);
        return CG_ERR_OOM;
    }
    for (i = 0; i < records->count; i++) {
        size_t idx = result_find_row(result, record_at(records, i)->id, used);
        if (idx == SIZE_MAX) {
            free(rows);
            free(used);
            return CG_ERR_INVALID_ARGUMENT;
        }
        rows[i] = result->rows[idx];
        used[idx] = 1;
    }
    free(result->rows);
    free(used);
    result->rows = rows;
    return CG_OK;
}

static cg_status make_cognator_id(char **out, const char *family, const char *concept, uint64_t mod) {
    size_t len = strlen(family ? family : "") + strlen(concept ? concept : "") + 64;
    char *s = (char *)malloc(len);
    if (s == NULL) {
        return CG_ERR_OOM;
    }
    snprintf(s, len, "%s:%s:%llu", family ? family : "", concept ? concept : "", (unsigned long long)mod);
    *out = s;
    return CG_OK;
}

static void options_effective(cg_options *dst, const cg_options *src) {
    cg_options_init_defaults(dst);
    if (src != NULL) {
        *dst = *src;
    }
    if (dst->iterations <= 0) dst->iterations = 3;
    if (dst->threshold_min <= 0) dst->threshold_min = 0.2;
    if (dst->threshold_max <= 0) dst->threshold_max = 0.8;
    if (dst->threshold_shrink <= 0) dst->threshold_shrink = 0.5;
    if (dst->graph_top_k <= 0) dst->graph_top_k = 2;
    if (dst->graph_adaptive_margin <= 0) dst->graph_adaptive_margin = 0.1;
    if (dst->graph_adaptive_ambiguity <= 0) dst->graph_adaptive_ambiguity = 0.5;
    if (dst->graph_mutual_boost <= 0) dst->graph_mutual_boost = 0.2;
    if (dst->graph_triangle_boost <= 0) dst->graph_triangle_boost = 0.3;
    if (dst->graph_contrastive_floor < 0) dst->graph_contrastive_floor = 0.05;
    if (dst->graph_risk_rank_weight < 0) dst->graph_risk_rank_weight = 0.5;
    if (dst->graph_risk_gap_weight < 0) dst->graph_risk_gap_weight = 0.5;
    if (dst->graph_rival_floor <= 0) dst->graph_rival_floor = 0.6;
    if (dst->refine_confidence_threshold <= 0) dst->refine_confidence_threshold = 0.55;
    if (dst->refine_bridge_ratio <= 0) dst->refine_bridge_ratio = 0.7;
    if (dst->bootstrap_sigma <= 0) dst->bootstrap_sigma = 0.05;
    if (dst->min_pairs <= 0) dst->min_pairs = 5;
    if (dst->dirichlet_concentration <= 0) dst->dirichlet_concentration = 50.0;
    if (dst->dirichlet_sharpness <= 0) dst->dirichlet_sharpness = 4.0;
}

static cg_graph_spec graph_spec_from_options(const cg_options *options) {
    cg_graph_spec spec;
    cg_graph_spec_init_defaults(&spec);
    spec.mode = options->graph_mode;
    spec.expansion_mode = options->graph_expansion_mode;
    spec.top_k = options->graph_top_k;
    spec.adaptive_margin = options->graph_adaptive_margin;
    spec.adaptive_either_max = options->graph_adaptive_either_max;
    spec.adaptive_ambiguity = options->graph_adaptive_ambiguity;
    spec.mutual_boost = options->graph_mutual_boost;
    spec.triangle_boost = options->graph_triangle_boost;
    spec.contrastive_penalty = options->graph_contrastive_penalty;
    spec.contrastive_floor = options->graph_contrastive_floor;
    spec.risk_rank_weight = options->graph_risk_rank_weight;
    spec.risk_gap_weight = options->graph_risk_gap_weight;
    spec.unilateral_penalty = options->graph_unilateral_penalty;
    spec.rival_floor = options->graph_rival_floor;
    return spec;
}

static cg_refine_spec refine_spec_from_options(const cg_options *options) {
    cg_refine_spec spec;
    cg_refine_spec_init_defaults(&spec);
    spec.mode = options->refine_mode;
    spec.confidence_threshold = options->refine_confidence_threshold;
    spec.bridge_ratio = options->refine_bridge_ratio;
    return spec;
}

static double resolve_concept_threshold(double *distances, size_t count, double family_threshold, const cg_options *options) {
    double local;
    double shrink;
    if (options->threshold > 0) {
        return options->threshold;
    }
    if (options->threshold_mode == NULL || strcmp(options->threshold_mode, "concept-shrink") != 0) {
        return family_threshold;
    }
    local = cg_clamp(cg_otsu_threshold(distances, (uint64_t)count, 0.0, 1.0, 64), options->threshold_min, options->threshold_max);
    shrink = options->threshold_shrink;
    if (shrink < 0.0) shrink = 0.0;
    if (shrink > 1.0) shrink = 1.0;
    return cg_clamp(shrink * family_threshold + (1.0 - shrink) * local, options->threshold_min, options->threshold_max);
}

cg_status cg_detect(cg_context *ctx, const cg_records *records, const cg_options *options_in, cg_result **out) {
    cg_options options;
    cg_result *result = NULL;
    cg_oov_report *oov = NULL;
    cg_seq *seqs = NULL;
    cg_lect_pair_table *tables = NULL;
    size_t table_count = 0;
    cg_distance_pair *all_pairs = NULL;
    double *all_distances = NULL;
    size_t all_pair_count = 0;
    size_t all_pair_cap = 0;
    size_t i;
    cg_status status = CG_OK;

    if (ctx == NULL || records == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    options_effective(&options, options_in);
    if (options.merkmal_system != NULL && options.merkmal_system[0] != '\0') {
        status = cg_context_use_system(ctx, options.merkmal_system);
        if (status != CG_OK) return status;
    }
    result = (cg_result *)calloc(1, sizeof(*result));
    oov = NULL;
    seqs = (cg_seq *)calloc(records->count, sizeof(*seqs));
    if (result == NULL || (records->count > 0 && seqs == NULL)) {
        status = CG_ERR_OOM;
        goto done;
    }
    status = cg_oov_report_new(&oov);
    if (status != CG_OK) goto done;
    for (i = 0; i < records->count; i++) {
        status = parse_record_sequence(ctx, record_at(records, i), options.class_reduce, oov, &seqs[i]);
        if (status != CG_OK) goto done;
    }
    status = build_pair_tables(ctx, records, seqs, &options, &tables, &table_count);
    if (status != CG_OK) goto done;
    for (i = 0; i < records->count; i++) {
        size_t j;
        const cg_record *ri = record_at(records, i);
        for (j = i + 1; j < records->count; j++) {
            const cg_record *rj = record_at(records, j);
            double d = 1.0;
            cg_distance_pair *next_pairs;
            double *next_distances;
            if (!str_eq(ri->concept, rj->concept) || str_eq(ri->language_id, rj->language_id)) {
                continue;
            }
            status = distance_for_pair(ctx, &seqs[i], &seqs[j], ri, rj, tables, table_count, &options, &d);
            if (status != CG_OK) goto done;
            if (all_pair_count == all_pair_cap) {
                size_t next_cap = all_pair_cap == 0 ? 64 : all_pair_cap * 2;
                next_pairs = (cg_distance_pair *)realloc(all_pairs, next_cap * sizeof(*next_pairs));
                if (next_pairs == NULL) {
                    status = CG_ERR_OOM;
                    goto done;
                }
                all_pairs = next_pairs;
                next_distances = (double *)realloc(all_distances, next_cap * sizeof(*next_distances));
                if (next_distances == NULL) {
                    status = CG_ERR_OOM;
                    goto done;
                }
                all_distances = next_distances;
                all_pair_cap = next_cap;
            }
            all_pairs[all_pair_count] = (cg_distance_pair){i, j, d};
            all_distances[all_pair_count] = d;
            all_pair_count++;
        }
    }
    result->threshold = options.threshold > 0 ? options.threshold : cg_clamp(cg_otsu_threshold(all_distances, (uint64_t)all_pair_count, 0.0, 1.0, 64), options.threshold_min, options.threshold_max);
    for (i = 0; i < records->count; i++) {
        const cg_record *ri = record_at(records, i);
        int concept_seen_before = 0;
        size_t k;
        size_t concept_count = 0;
        size_t *concept_indices = NULL;
        cg_distance_pair *concept_pairs = NULL;
        double *concept_distances = NULL;
        size_t concept_pair_count = 0;
        cg_edge *edges = NULL;
        uint64_t edge_count = 0;
        cg_graph *graph = NULL;
        cg_partition *part = NULL;
        cg_partition *refined = NULL;
        cg_diagnostics *diag = NULL;
        cg_bootstrap_summary *boot = NULL;
        cg_graph_spec gs = graph_spec_from_options(&options);
        cg_refine_spec rs = refine_spec_from_options(&options);
        double threshold;
        for (k = 0; k < i; k++) {
            if (str_eq(record_at(records, k)->concept, ri->concept)) {
                concept_seen_before = 1;
                break;
            }
        }
        if (concept_seen_before) continue;
        for (k = 0; k < records->count; k++) {
            if (str_eq(record_at(records, k)->concept, ri->concept)) {
                size_t *next = (size_t *)realloc(concept_indices, (concept_count + 1) * sizeof(*next));
                if (next == NULL) { status = CG_ERR_OOM; goto concept_done; }
                concept_indices = next;
                concept_indices[concept_count++] = k;
            }
        }
        concept_pairs = (cg_distance_pair *)calloc(all_pair_count ? all_pair_count : 1, sizeof(*concept_pairs));
        concept_distances = (double *)calloc(all_pair_count ? all_pair_count : 1, sizeof(*concept_distances));
        if (concept_pairs == NULL || concept_distances == NULL) { status = CG_ERR_OOM; goto concept_done; }
        for (k = 0; k < all_pair_count; k++) {
            size_t a_local = SIZE_MAX, b_local = SIZE_MAX, m;
            for (m = 0; m < concept_count; m++) {
                if (concept_indices[m] == all_pairs[k].a) a_local = m;
                if (concept_indices[m] == all_pairs[k].b) b_local = m;
            }
            if (a_local != SIZE_MAX && b_local != SIZE_MAX) {
                concept_pairs[concept_pair_count] = (cg_distance_pair){a_local, b_local, all_pairs[k].distance};
                concept_distances[concept_pair_count] = all_pairs[k].distance;
                concept_pair_count++;
            }
        }
        threshold = resolve_concept_threshold(concept_distances, concept_pair_count, result->threshold, &options);
        status = cg_build_graph_edges((uint64_t)concept_count, concept_pairs, (uint64_t)concept_pair_count, threshold, &gs, &edges, &edge_count);
        if (status == CG_OK) status = cg_graph_new((uint64_t)concept_count, edges, edge_count, &graph);
        if (status == CG_OK) status = cg_cluster_infomap(graph, options.seed, &part);
        if (status == CG_OK) status = cg_refine_partition(graph, part, &rs, &refined);
        if (status == CG_OK) status = cg_diagnostics_new(graph, refined, &diag);
        if (status == CG_OK && options.bootstrap > 0) {
            status = cg_bootstrap_confidences((uint64_t)concept_count, concept_pairs, (uint64_t)concept_pair_count, refined, threshold, options.bootstrap, options.bootstrap_sigma, options.seed, &gs, &boot);
        }
        if (status == CG_OK) {
            for (k = 0; k < concept_count; k++) {
                const cg_record *r = record_at(records, concept_indices[k]);
                const cg_diagnostic *d = cg_diagnostics_get(diag, (uint64_t)k);
                cg_sidecar_row row;
                memset(&row, 0, sizeof(row));
                row.id = cg_strdup(r->id);
                status = make_cognator_id((char **)&row.cognator_id, r->family, r->concept, cg_partition_module_at(refined, (uint64_t)k));
                if (row.id == NULL || status != CG_OK) {
                    cg_free_sidecar_row(&row);
                    if (status == CG_OK) status = CG_ERR_OOM;
                    break;
                }
                row.confidence = d->confidence;
                row.assigned_support = d->assigned_support;
                row.alternative_support = d->alternative_support;
                row.support_margin = d->support_margin;
                row.cluster_size = (int)d->cluster_size;
                row.candidate_neighbors = (int)d->candidate_neighbors;
                if (boot != NULL) {
                    row.bootstrap = cg_bootstrap_mean_at(boot, (uint64_t)k);
                    row.bootstrap_low = cg_bootstrap_low_at(boot, (uint64_t)k);
                    row.bootstrap_high = cg_bootstrap_high_at(boot, (uint64_t)k);
                }
                status = result_push(result, row);
                if (status != CG_OK) {
                    cg_free_sidecar_row(&row);
                    break;
                }
            }
        }
concept_done:
        free(concept_indices);
        free(concept_pairs);
        free(concept_distances);
        cg_edges_free(edges);
        cg_graph_free(graph);
        cg_partition_free(part);
        cg_partition_free(refined);
        cg_diagnostics_free(diag);
        cg_bootstrap_summary_free(boot);
        if (status != CG_OK) goto done;
    }
    status = result_reorder_like_records(result, records);
    if (status != CG_OK) goto done;
    result->oov_count = cg_oov_report_count(oov);
    if (result->oov_count > 0) {
        result->oov = (cg_oov_row *)calloc(result->oov_count, sizeof(*result->oov));
        if (result->oov == NULL) { status = CG_ERR_OOM; goto done; }
        for (i = 0; i < result->oov_count; i++) {
            const cg_oov_row *row = cg_oov_report_get(oov, (uint64_t)i);
            result->oov[i].grapheme = cg_strdup(row->grapheme);
            result->oov[i].count = row->count;
            if (result->oov[i].grapheme == NULL) { status = CG_ERR_OOM; goto done; }
        }
    }
    *out = result;
    result = NULL;
done:
    if (seqs != NULL) {
        for (i = 0; i < records->count; i++) seq_free(&seqs[i]);
    }
    free(seqs);
    free_pair_tables(tables, table_count);
    free(all_pairs);
    free(all_distances);
    cg_oov_report_free(oov);
    cg_result_free(result);
    return status;
}

void cg_result_free(cg_result *result) {
    size_t i;
    if (result == NULL) return;
    for (i = 0; i < result->row_count; i++) cg_free_sidecar_row(&result->rows[i]);
    free(result->rows);
    for (i = 0; i < result->oov_count; i++) cg_free_oov_row(&result->oov[i]);
    free(result->oov);
    free(result);
}

uint64_t cg_result_row_count(const cg_result *result) {
    return result == NULL ? 0 : (uint64_t)result->row_count;
}

const cg_sidecar_row *cg_result_row_at(const cg_result *result, uint64_t index) {
    if (result == NULL || index >= result->row_count) return NULL;
    return &result->rows[index];
}

double cg_result_threshold(const cg_result *result) {
    return result == NULL ? 0.0 : result->threshold;
}

uint64_t cg_result_oov_count(const cg_result *result) {
    return result == NULL ? 0 : (uint64_t)result->oov_count;
}

const cg_oov_row *cg_result_oov_at(const cg_result *result, uint64_t index) {
    if (result == NULL || index >= result->oov_count) return NULL;
    return &result->oov[index];
}

cg_status cg_write_sidecar(const char *path, const cg_result *result) {
    if (result == NULL) return CG_ERR_INVALID_ARGUMENT;
    return cg_write_sidecar_rows(path, result->rows, (uint64_t)result->row_count);
}

cg_status cg_write_oov_report(const char *path, const cg_result *result) {
    if (result == NULL) return CG_ERR_INVALID_ARGUMENT;
    return cg_write_oov_report_rows(path, result->oov, (uint64_t)result->oov_count);
}
