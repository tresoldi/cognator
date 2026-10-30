#include "internal/align.h"
#include "internal/context.h"
#include "internal/data.h"
#include "internal/pairs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cg_seq {
    char **items;
    size_t count;
    size_t cap;
    /* Offsets into items where a `+` stood; only kept for the morpheme
     * export, and never read by clustering. */
    size_t *breaks;
    size_t break_count;
    size_t break_cap;
} cg_seq;

typedef struct cg_form_pair_list {
    cg_form_pair *items;
    size_t count;
    size_t cap;
} cg_form_pair_list;

typedef struct cg_concept_group {
    const char *concept;
    size_t *indices;
    size_t count;
    size_t cap;
} cg_concept_group;

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
    free(seq->breaks);
    *seq = (cg_seq){0};
}

static cg_status seq_push(cg_seq *seq, const char *value) {
    char **next;
    if (seq->count == seq->cap) {
        size_t next_cap = seq->cap == 0 ? 8 : seq->cap * 2;
        next = (char **)realloc(seq->items, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        seq->items = next;
        seq->cap = next_cap;
    }
    seq->items[seq->count] = cg_strdup(value);
    if (seq->items[seq->count] == NULL) {
        return CG_ERR_OOM;
    }
    seq->count++;
    return CG_OK;
}

static cg_status seq_push_break(cg_seq *seq) {
    if (seq->break_count == seq->break_cap) {
        size_t next_cap = seq->break_cap == 0 ? 4 : seq->break_cap * 2;
        size_t *next = (size_t *)realloc(seq->breaks, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        seq->breaks = next;
        seq->break_cap = next_cap;
    }
    seq->breaks[seq->break_count++] = seq->count;
    return CG_OK;
}

static cg_status parse_record_sequence(const cg_context *ctx, const cg_record *record, int class_reduce, int keep_breaks, cg_oov_report *oov, cg_seq *seq) {
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
        if (keep_breaks && token != NULL && token->boundary) {
            status = seq_push_break(seq);
            if (status != CG_OK) {
                cg_tokens_free(tokens);
                return status;
            }
            continue;
        }
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

static cg_status form_pair_push(cg_form_pair_list *pairs, const cg_seq *source, const cg_seq *target, const cg_record *rs, const cg_record *rt) {
    cg_form_pair *next;
    if (pairs->count == pairs->cap) {
        size_t next_cap = pairs->cap == 0 ? 8 : pairs->cap * 2;
        next = (cg_form_pair *)realloc(pairs->items, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        pairs->items = next;
        pairs->cap = next_cap;
    }
    pairs->items[pairs->count] = (cg_form_pair){
        .source = (const char *const *)source->items,
        .source_count = (uint64_t)source->count,
        .target = (const char *const *)target->items,
        .target_count = (uint64_t)target->count,
        .concept = rs->concept,
        .source_id = rs->id,
        .target_id = rt->id
    };
    pairs->count++;
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
            cg_form_pair_list pairs = {0};
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
                        status = form_pair_push(&pairs, &seqs[a], &seqs[b], ra, rb);
                    } else {
                        status = form_pair_push(&pairs, &seqs[b], &seqs[a], rb, ra);
                    }
                    if (status != CG_OK) {
                        free(pairs.items);
                        goto done;
                    }
                }
            }
            if (options->scorer_mode != NULL && strcmp(options->scorer_mode, "posterior") == 0 && (int)pairs.count >= options->min_pairs && pairs.count > 0) {
                char **inventory = NULL;
                uint64_t inventory_count = 0;
                status = cg_inventory_from_pairs(pairs.items, (uint64_t)pairs.count, &inventory, &inventory_count);
                if (status == CG_OK) {
                    status = cg_dirichlet_prior_new(ctx, options->dirichlet_concentration, options->dirichlet_sharpness, (const char *const *)inventory, inventory_count, &prior);
                }
                if (status == CG_OK) {
                    status = cg_train_posterior(ctx, lects[i], lects[j], pairs.items, (uint64_t)pairs.count, prior, options->iterations, &table);
                }
                if (status == CG_OK && options->dirichlet_scale_mode != NULL && options->dirichlet_scale_mode[0] != '\0') {
                    status = cg_posterior_table_attach_scaling(table, options->dirichlet_scale_mode);
                }
                cg_string_array_free(inventory, inventory_count);
                if (status != CG_OK) {
                    cg_posterior_table_free(table);
                    cg_dirichlet_prior_free(prior);
                    free(pairs.items);
                    goto done;
                }
                has_posterior = 1;
            }
            free(pairs.items);
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

static void concept_groups_free(cg_concept_group *groups, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        free(groups[i].indices);
    }
    free(groups);
}

static size_t find_concept_group(cg_concept_group *groups, size_t count, const char *concept) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (str_eq(groups[i].concept, concept)) {
            return i;
        }
    }
    return SIZE_MAX;
}

static cg_status concept_group_push_index(cg_concept_group *group, size_t index) {
    size_t *next;
    if (group->count == group->cap) {
        size_t next_cap = group->cap == 0 ? 8 : group->cap * 2;
        next = (size_t *)realloc(group->indices, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        group->indices = next;
        group->cap = next_cap;
    }
    group->indices[group->count++] = index;
    return CG_OK;
}

static cg_status build_concept_groups(const cg_records *records, cg_concept_group **out, size_t *out_count) {
    cg_concept_group *groups = NULL;
    size_t count = 0;
    size_t i;
    cg_status status = CG_OK;

    *out = NULL;
    *out_count = 0;
    for (i = 0; i < records->count; i++) {
        const cg_record *record = record_at(records, i);
        size_t idx = find_concept_group(groups, count, record->concept);
        if (idx == SIZE_MAX) {
            cg_concept_group *next = (cg_concept_group *)realloc(groups, (count + 1) * sizeof(*next));
            if (next == NULL) {
                status = CG_ERR_OOM;
                goto done;
            }
            groups = next;
            groups[count] = (cg_concept_group){record->concept, NULL, 0, 0};
            idx = count++;
        }
        status = concept_group_push_index(&groups[idx], i);
        if (status != CG_OK) {
            goto done;
        }
    }
done:
    if (status != CG_OK) {
        concept_groups_free(groups, count);
        return status;
    }
    *out = groups;
    *out_count = count;
    return CG_OK;
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

/* distance_for_pair for the pair export: the same scorer choice, direction and
 * alignment mode, but over any span of the two rows and with the alignment
 * components kept. The returned distance is bit-for-bit what distance_for_pair
 * gives for the whole words (see components_generic in align.c).
 *
 * The export aligns far more pairs than clustering does -- every null sample
 * -- and a segment score costs a posterior lookup plus two merkmal role
 * lookups. Scores are a pure function of (scorer, source, target), so they
 * are memoized here; the cached double is the one the scorer returned, so
 * nothing downstream can tell. */
typedef struct score_memo_entry {
    char *source;
    char *target;
    size_t table;
    double score;
    int used;
} score_memo_entry;

typedef struct score_memo {
    score_memo_entry *slots;
    size_t slot_count;
    size_t used;
} score_memo;

typedef struct pairs_oracle_user {
    const cg_context *ctx;
    const cg_records *records;
    cg_lect_pair_table *tables;
    size_t table_count;
    const cg_options *options;
    score_memo memo;
} pairs_oracle_user;

typedef struct memo_score_user {
    pairs_oracle_user *oracle;
    size_t table;          /* SIZE_MAX for the merkmal prior */
} memo_score_user;

static uint64_t memo_hash(size_t table, const char *source, const char *target) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t)table;
    const unsigned char *p;
    for (p = (const unsigned char *)source; *p; p++) {
        h ^= *p;
        h *= 1099511628211ull;
    }
    h ^= 0x1f;
    h *= 1099511628211ull;
    for (p = (const unsigned char *)target; *p; p++) {
        h ^= *p;
        h *= 1099511628211ull;
    }
    return h;
}

static void memo_free(score_memo *memo) {
    size_t i;
    for (i = 0; i < memo->slot_count; i++) {
        free(memo->slots[i].source);
        free(memo->slots[i].target);
    }
    free(memo->slots);
    *memo = (score_memo){0};
}

static cg_status memo_grow(score_memo *memo) {
    size_t next_count = memo->slot_count == 0 ? 1024 : memo->slot_count * 2;
    score_memo_entry *next = (score_memo_entry *)calloc(next_count, sizeof(*next));
    size_t i;
    if (next == NULL) return CG_ERR_OOM;
    for (i = 0; i < memo->slot_count; i++) {
        score_memo_entry *e = &memo->slots[i];
        size_t slot;
        if (!e->used) continue;
        slot = (size_t)(memo_hash(e->table, e->source, e->target) % next_count);
        while (next[slot].used) slot = (slot + 1) % next_count;
        next[slot] = *e;
    }
    free(memo->slots);
    memo->slots = next;
    memo->slot_count = next_count;
    return CG_OK;
}

static cg_status memo_score_cb(const char *source, const char *target, void *user, double *out) {
    memo_score_user *mu = (memo_score_user *)user;
    pairs_oracle_user *u = mu->oracle;
    score_memo *memo = &u->memo;
    size_t slot;
    double value = 0.0;
    cg_status status;
    if (memo->slot_count == 0 || (memo->used + 1) * 2 > memo->slot_count) {
        status = memo_grow(memo);
        if (status != CG_OK) return status;
    }
    slot = (size_t)(memo_hash(mu->table, source, target) % memo->slot_count);
    while (memo->slots[slot].used) {
        score_memo_entry *e = &memo->slots[slot];
        if (e->table == mu->table && strcmp(e->source, source) == 0 && strcmp(e->target, target) == 0) {
            *out = e->score;
            return CG_OK;
        }
        slot = (slot + 1) % memo->slot_count;
    }
    if (mu->table == SIZE_MAX) {
        status = cg_prior_score(u->ctx, source, target, 1, &value);
    } else {
        cg_posterior_score_user inner = {u->ctx, u->tables[mu->table].table, 1};
        status = posterior_score_cb(source, target, &inner, &value);
    }
    if (status != CG_OK) return status;
    memo->slots[slot].source = cg_strdup(source);
    memo->slots[slot].target = cg_strdup(target);
    if (memo->slots[slot].source == NULL || memo->slots[slot].target == NULL) {
        free(memo->slots[slot].source);
        free(memo->slots[slot].target);
        memo->slots[slot].source = NULL;
        memo->slots[slot].target = NULL;
        return CG_ERR_OOM;
    }
    memo->slots[slot].table = mu->table;
    memo->slots[slot].score = value;
    memo->slots[slot].used = 1;
    memo->used++;
    *out = value;
    return CG_OK;
}

static cg_status pairs_oracle(void *user, size_t rec_a, const char *const *a, size_t a_count, size_t rec_b, const char *const *b, size_t b_count, cg_internal_components *components, double *distance) {
    pairs_oracle_user *u = (pairs_oracle_user *)user;
    const cg_record *ra = record_at(u->records, rec_a);
    const cg_record *rb = record_at(u->records, rec_b);
    size_t idx = find_pair_table(u->tables, u->table_count, ra->language_id, rb->language_id);
    int global = u->options->alignment_mode != NULL && strcmp(u->options->alignment_mode, "global") == 0;
    memo_score_user mu;
    cg_status status;
    mu.oracle = u;
    if (idx != SIZE_MAX && u->tables[idx].has_posterior) {
        int reverse = string_less(rb->language_id, ra->language_id);
        mu.table = idx;
        status = cg_internal_distance_components(
            reverse ? b : a, reverse ? b_count : a_count,
            reverse ? a : b, reverse ? a_count : b_count,
            memo_score_cb, &mu, components);
    } else {
        /* As cg_align_distance_combined: the merkmal prior, prosody-aware,
         * in row order. */
        mu.table = SIZE_MAX;
        status = cg_internal_distance_components(a, a_count, b, b_count, memo_score_cb, &mu, components);
    }
    if (status != CG_OK) {
        return status;
    }
    *distance = global ? components->global : components->combined;
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

static cg_status detect_impl(cg_context *ctx, const cg_records *records, const cg_options *options_in, const cg_pairs_spec *pairs_spec, cg_result **out) {
    cg_options options;
    cg_pairs_spec pspec;
    int want_pairs = pairs_spec != NULL && (pairs_spec->unit == CG_PAIRS_WORDS || pairs_spec->unit == CG_PAIRS_MORPHEMES);
    int keep_breaks = want_pairs && pairs_spec->unit == CG_PAIRS_MORPHEMES;
    cg_result *result = NULL;
    cg_oov_report *oov = NULL;
    cg_seq *seqs = NULL;
    cg_lect_pair_table *tables = NULL;
    size_t table_count = 0;
    cg_concept_group *concept_groups = NULL;
    size_t concept_group_count = 0;
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
    if (pairs_spec != NULL && pairs_spec->unit != CG_PAIRS_NONE && !want_pairs) {
        return CG_ERR_UNSUPPORTED_OPTION;
    }
    if (want_pairs) {
        cg_internal_pairs_spec_effective(&pspec, pairs_spec);
    }
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
        status = parse_record_sequence(ctx, record_at(records, i), options.class_reduce, keep_breaks, oov, &seqs[i]);
        if (status != CG_OK) goto done;
    }
    status = build_pair_tables(ctx, records, seqs, &options, &tables, &table_count);
    if (status != CG_OK) goto done;
    status = build_concept_groups(records, &concept_groups, &concept_group_count);
    if (status != CG_OK) goto done;
    if (records->count > 0) {
        result->rows = (cg_sidecar_row *)calloc(records->count, sizeof(*result->rows));
        if (result->rows == NULL) {
            status = CG_ERR_OOM;
            goto done;
        }
        result->row_count = records->count;
        result->row_cap = records->count;
    }
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
    for (i = 0; i < concept_group_count; i++) {
        const cg_concept_group *group = &concept_groups[i];
        size_t k;
        size_t concept_count = group->count;
        size_t *index_to_local = NULL;
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
        index_to_local = (size_t *)malloc((records->count == 0 ? 1 : records->count) * sizeof(*index_to_local));
        if (index_to_local == NULL) { status = CG_ERR_OOM; goto concept_done; }
        for (k = 0; k < records->count; k++) {
            index_to_local[k] = SIZE_MAX;
        }
        for (k = 0; k < concept_count; k++) {
            index_to_local[group->indices[k]] = k;
        }
        concept_pairs = (cg_distance_pair *)calloc(all_pair_count ? all_pair_count : 1, sizeof(*concept_pairs));
        concept_distances = (double *)calloc(all_pair_count ? all_pair_count : 1, sizeof(*concept_distances));
        if (concept_pairs == NULL || concept_distances == NULL) { status = CG_ERR_OOM; goto concept_done; }
        for (k = 0; k < all_pair_count; k++) {
            size_t a_local = index_to_local[all_pairs[k].a];
            size_t b_local = index_to_local[all_pairs[k].b];
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
                size_t record_index = group->indices[k];
                const cg_record *r = record_at(records, record_index);
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
                result->rows[record_index] = row;
            }
        }
concept_done:
        free(index_to_local);
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
    if (want_pairs) {
        cg_pairs_form *forms = (cg_pairs_form *)calloc(records->count + 1, sizeof(*forms));
        pairs_oracle_user oracle = {ctx, records, tables, table_count, &options, {NULL, 0, 0}};
        cg_pairs_input input;
        if (forms == NULL) { status = CG_ERR_OOM; goto done; }
        for (i = 0; i < records->count; i++) {
            forms[i].items = (const char *const *)seqs[i].items;
            forms[i].count = seqs[i].count;
            forms[i].breaks = seqs[i].breaks;
            forms[i].break_count = seqs[i].break_count;
        }
        memset(&input, 0, sizeof(input));
        input.records = records;
        input.forms = forms;
        input.distance = pairs_oracle;
        input.user = &oracle;
        input.spec = pspec;
        input.word_threshold = result->threshold;
        input.fixed_threshold = options.threshold;
        input.threshold_min = options.threshold_min;
        input.threshold_max = options.threshold_max;
        input.graph_spec = graph_spec_from_options(&options);
        input.refine_spec = refine_spec_from_options(&options);
        input.seed = options.seed;
        status = cg_internal_pairs_run(&input, result);
        memo_free(&oracle.memo);
        free(forms);
        if (status != CG_OK) goto done;
    }
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
    concept_groups_free(concept_groups, concept_group_count);
    free(all_pairs);
    free(all_distances);
    cg_oov_report_free(oov);
    cg_result_free(result);
    return status;
}

cg_status cg_detect(cg_context *ctx, const cg_records *records, const cg_options *options_in, cg_result **out) {
    return detect_impl(ctx, records, options_in, NULL, out);
}

cg_status cg_detect_with_pairs(cg_context *ctx, const cg_records *records, const cg_options *options_in, const cg_pairs_spec *spec, cg_result **out) {
    return detect_impl(ctx, records, options_in, spec, out);
}

void cg_result_free(cg_result *result) {
    size_t i;
    if (result == NULL) return;
    for (i = 0; i < result->pair_count; i++) cg_free_pair_row(&result->pairs[i]);
    free(result->pairs);
    if (result->morpheme_ids != NULL) {
        for (i = 0; i < result->row_count; i++) free(result->morpheme_ids[i]);
        free(result->morpheme_ids);
    }
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
