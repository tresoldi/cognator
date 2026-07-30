#include "internal/align.h"
#include "internal/context.h"
#include "internal/data.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CG_GAP "-"
#define CG_DEFAULT_DIRICHLET_CONCENTRATION 50.0
#define CG_DEFAULT_DIRICHLET_SHARPNESS 4.0
#define CG_POSTERIOR_SCORER_SCALE 3.0
#define CG_ROLE_MISMATCH_PENALTY 0.3

typedef struct cg_count_cell {
    char *source;
    char *target;
    uint64_t count;
} cg_count_cell;

typedef struct cg_total_cell {
    char *key;
    uint64_t count;
} cg_total_cell;

struct cg_dirichlet_prior {
    const cg_context *ctx;
    double concentration;
    double sharpness;
    char **inventory;
    double *denominators;
    size_t count;
};

struct cg_posterior_table {
    char *source_lect;
    char *target_lect;
    const cg_dirichlet_prior *prior;
    cg_count_cell *counts;
    size_t count_len;
    size_t count_cap;
    cg_total_cell *row_total;
    size_t row_len;
    size_t row_cap;
    cg_total_cell *target_total;
    size_t target_len;
    size_t target_cap;
    uint64_t columns;
    uint64_t total_pairs;
    char *scale_mode;
    double median_row_total;
};

typedef struct cg_posterior_score_user {
    const cg_context *ctx;
    const cg_posterior_table *table;
    int prosody_aware;
} cg_posterior_score_user;

typedef struct cg_prior_score_user {
    const cg_context *ctx;
    int prosody_aware;
} cg_prior_score_user;

static int string_ptr_cmp(const void *a, const void *b) {
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;

    return strcmp(*sa, *sb);
}

static cg_status score_prior_cb(const char *source, const char *target, void *user, double *out) {
    cg_prior_score_user *u = (cg_prior_score_user *)user;

    return cg_prior_score(u->ctx, source, target, u->prosody_aware, out);
}

static int string_array_contains(char **items, size_t count, const char *value) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (strcmp(items[i], value) == 0) {
            return 1;
        }
    }
    return 0;
}

static cg_status string_array_push_unique(char ***items, size_t *count, size_t *cap, const char *value) {
    char **next;

    if (value == NULL || value[0] == '\0' || string_array_contains(*items, *count, value)) {
        return CG_OK;
    }
    if (*count == *cap) {
        size_t next_cap = *cap == 0 ? 16 : *cap * 2;
        next = (char **)realloc(*items, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        *items = next;
        *cap = next_cap;
    }
    (*items)[*count] = cg_strdup(value);
    if ((*items)[*count] == NULL) {
        return CG_ERR_OOM;
    }
    (*count)++;
    return CG_OK;
}

static double prior_distance(const cg_dirichlet_prior *prior, const char *source, const char *target) {
    double distance = 1.0;

    if (strcmp(source, CG_GAP) == 0 || strcmp(target, CG_GAP) == 0) {
        return strcmp(source, target) == 0 ? 0.0 : 1.0;
    }
    if (cg_context_segment_distance(prior->ctx, source, target, &distance) != CG_OK) {
        return 1.0;
    }
    return distance;
}

static int prior_inventory_index(const cg_dirichlet_prior *prior, const char *value) {
    size_t i;

    if (prior == NULL || value == NULL) {
        return -1;
    }
    for (i = 0; i < prior->count; i++) {
        if (strcmp(prior->inventory[i], value) == 0) {
            return (int)i;
        }
    }
    return -1;
}

cg_status cg_dirichlet_prior_new(
    const cg_context *ctx,
    double concentration,
    double sharpness,
    const char *const *inventory,
    uint64_t inventory_count,
    cg_dirichlet_prior **out
) {
    cg_dirichlet_prior *prior;
    size_t cap = 0;
    size_t i;

    if (ctx == NULL || out == NULL || (inventory == NULL && inventory_count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    prior = (cg_dirichlet_prior *)calloc(1, sizeof(*prior));
    if (prior == NULL) {
        return CG_ERR_OOM;
    }
    prior->ctx = ctx;
    prior->concentration = concentration > 0.0 ? concentration : CG_DEFAULT_DIRICHLET_CONCENTRATION;
    prior->sharpness = sharpness > 0.0 ? sharpness : CG_DEFAULT_DIRICHLET_SHARPNESS;
    for (i = 0; i < inventory_count; i++) {
        cg_status status = string_array_push_unique(&prior->inventory, &prior->count, &cap, inventory[i]);
        if (status != CG_OK) {
            cg_dirichlet_prior_free(prior);
            return status;
        }
    }
    qsort(prior->inventory, prior->count, sizeof(*prior->inventory), string_ptr_cmp);
    prior->denominators = (double *)calloc(prior->count, sizeof(*prior->denominators));
    if (prior->denominators == NULL && prior->count > 0) {
        cg_dirichlet_prior_free(prior);
        return CG_ERR_OOM;
    }
    for (i = 0; i < prior->count; i++) {
        size_t j;
        for (j = 0; j < prior->count; j++) {
            prior->denominators[i] += exp(-prior->sharpness * prior_distance(prior, prior->inventory[i], prior->inventory[j]));
        }
    }
    *out = prior;
    return CG_OK;
}

void cg_dirichlet_prior_free(cg_dirichlet_prior *prior) {
    size_t i;

    if (prior == NULL) {
        return;
    }
    for (i = 0; i < prior->count; i++) {
        free(prior->inventory[i]);
    }
    free(prior->inventory);
    free(prior->denominators);
    free(prior);
}

uint64_t cg_dirichlet_prior_inventory_count(const cg_dirichlet_prior *prior) {
    return prior == NULL ? 0 : (uint64_t)prior->count;
}

const char *cg_dirichlet_prior_inventory_at(const cg_dirichlet_prior *prior, uint64_t index) {
    if (prior == NULL || index >= prior->count) {
        return NULL;
    }
    return prior->inventory[index];
}

cg_status cg_dirichlet_prior_alpha(
    const cg_dirichlet_prior *prior,
    const char *source,
    const char *target,
    double *out
) {
    int target_idx;
    int source_idx;
    double denom;
    double num;

    if (prior == NULL || source == NULL || target == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = 0.0;
    target_idx = prior_inventory_index(prior, target);
    if (target_idx < 0) {
        return CG_OK;
    }
    source_idx = prior_inventory_index(prior, source);
    if (source_idx < 0) {
        if (prior->count == 0) {
            return CG_OK;
        }
        *out = prior->concentration / (double)prior->count;
        return CG_OK;
    }
    denom = prior->denominators[source_idx];
    if (denom <= 0.0) {
        *out = prior->count == 0 ? 0.0 : prior->concentration / (double)prior->count;
        return CG_OK;
    }
    num = exp(-prior->sharpness * prior_distance(prior, source, target));
    *out = prior->concentration * num / denom;
    return CG_OK;
}

void cg_string_array_free(char **items, uint64_t count) {
    uint64_t i;

    if (items == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

cg_status cg_inventory_from_pairs(
    const cg_form_pair *pairs,
    uint64_t pair_count,
    char ***inventory_out,
    uint64_t *inventory_count_out
) {
    char **items = NULL;
    size_t count = 0;
    size_t cap = 0;
    uint64_t i;

    if (inventory_out == NULL || inventory_count_out == NULL || (pairs == NULL && pair_count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *inventory_out = NULL;
    *inventory_count_out = 0;
    if (string_array_push_unique(&items, &count, &cap, CG_GAP) != CG_OK) {
        return CG_ERR_OOM;
    }
    for (i = 0; i < pair_count; i++) {
        uint64_t j;
        if ((pairs[i].source == NULL && pairs[i].source_count > 0) ||
            (pairs[i].target == NULL && pairs[i].target_count > 0)) {
            cg_string_array_free(items, (uint64_t)count);
            return CG_ERR_INVALID_ARGUMENT;
        }
        for (j = 0; j < pairs[i].source_count; j++) {
            cg_status status = string_array_push_unique(&items, &count, &cap, pairs[i].source[j]);
            if (status != CG_OK) {
                cg_string_array_free(items, (uint64_t)count);
                return status;
            }
        }
        for (j = 0; j < pairs[i].target_count; j++) {
            cg_status status = string_array_push_unique(&items, &count, &cap, pairs[i].target[j]);
            if (status != CG_OK) {
                cg_string_array_free(items, (uint64_t)count);
                return status;
            }
        }
    }
    qsort(items, count, sizeof(*items), string_ptr_cmp);
    *inventory_out = items;
    *inventory_count_out = (uint64_t)count;
    return CG_OK;
}

static cg_status total_inc(cg_total_cell **items, size_t *len, size_t *cap, const char *key) {
    size_t i;

    for (i = 0; i < *len; i++) {
        if (strcmp((*items)[i].key, key) == 0) {
            (*items)[i].count++;
            return CG_OK;
        }
    }
    if (*len == *cap) {
        size_t next_cap = *cap == 0 ? 16 : *cap * 2;
        cg_total_cell *next = (cg_total_cell *)realloc(*items, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        *items = next;
        *cap = next_cap;
    }
    (*items)[*len].key = cg_strdup(key);
    if ((*items)[*len].key == NULL) {
        return CG_ERR_OOM;
    }
    (*items)[*len].count = 1;
    (*len)++;
    return CG_OK;
}

static uint64_t total_get(const cg_total_cell *items, size_t len, const char *key) {
    size_t i;

    for (i = 0; i < len; i++) {
        if (strcmp(items[i].key, key) == 0) {
            return items[i].count;
        }
    }
    return 0;
}

cg_status cg_posterior_table_new(
    const char *source_lect,
    const char *target_lect,
    const cg_dirichlet_prior *prior,
    cg_posterior_table **out
) {
    cg_posterior_table *table;

    if (source_lect == NULL || target_lect == NULL || prior == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    table = (cg_posterior_table *)calloc(1, sizeof(*table));
    if (table == NULL) {
        return CG_ERR_OOM;
    }
    table->source_lect = cg_strdup(source_lect);
    table->target_lect = cg_strdup(target_lect);
    table->prior = prior;
    if (table->source_lect == NULL || table->target_lect == NULL) {
        cg_posterior_table_free(table);
        return CG_ERR_OOM;
    }
    *out = table;
    return CG_OK;
}

void cg_posterior_table_free(cg_posterior_table *table) {
    size_t i;

    if (table == NULL) {
        return;
    }
    free(table->source_lect);
    free(table->target_lect);
    for (i = 0; i < table->count_len; i++) {
        free(table->counts[i].source);
        free(table->counts[i].target);
    }
    free(table->counts);
    for (i = 0; i < table->row_len; i++) {
        free(table->row_total[i].key);
    }
    free(table->row_total);
    for (i = 0; i < table->target_len; i++) {
        free(table->target_total[i].key);
    }
    free(table->target_total);
    free(table->scale_mode);
    free(table);
}

cg_status cg_posterior_table_add(
    cg_posterior_table *table,
    const char *source,
    const char *target
) {
    size_t i;
    const char *s;
    const char *t;

    if (table == NULL || source == NULL || target == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    s = source[0] == '\0' ? CG_GAP : source;
    t = target[0] == '\0' ? CG_GAP : target;
    for (i = 0; i < table->count_len; i++) {
        if (strcmp(table->counts[i].source, s) == 0 && strcmp(table->counts[i].target, t) == 0) {
            table->counts[i].count++;
            goto totals;
        }
    }
    if (table->count_len == table->count_cap) {
        size_t next_cap = table->count_cap == 0 ? 32 : table->count_cap * 2;
        cg_count_cell *next = (cg_count_cell *)realloc(table->counts, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        table->counts = next;
        table->count_cap = next_cap;
    }
    table->counts[table->count_len].source = cg_strdup(s);
    table->counts[table->count_len].target = cg_strdup(t);
    if (table->counts[table->count_len].source == NULL || table->counts[table->count_len].target == NULL) {
        free(table->counts[table->count_len].source);
        free(table->counts[table->count_len].target);
        return CG_ERR_OOM;
    }
    table->counts[table->count_len].count = 1;
    table->count_len++;
totals:
    if (total_inc(&table->row_total, &table->row_len, &table->row_cap, s) != CG_OK) {
        return CG_ERR_OOM;
    }
    if (total_inc(&table->target_total, &table->target_len, &table->target_cap, t) != CG_OK) {
        return CG_ERR_OOM;
    }
    table->columns++;
    return CG_OK;
}

uint64_t cg_posterior_table_count_pair(
    const cg_posterior_table *table,
    const char *source,
    const char *target
) {
    size_t i;
    const char *s;
    const char *t;

    if (table == NULL || source == NULL || target == NULL) {
        return 0;
    }
    s = source[0] == '\0' ? CG_GAP : source;
    t = target[0] == '\0' ? CG_GAP : target;
    for (i = 0; i < table->count_len; i++) {
        if (strcmp(table->counts[i].source, s) == 0 && strcmp(table->counts[i].target, t) == 0) {
            return table->counts[i].count;
        }
    }
    return 0;
}

uint64_t cg_posterior_table_columns(const cg_posterior_table *table) {
    return table == NULL ? 0 : table->columns;
}

uint64_t cg_posterior_table_total_pairs(const cg_posterior_table *table) {
    return table == NULL ? 0 : table->total_pairs;
}

static int double_cmp(const void *a, const void *b) {
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

cg_status cg_posterior_table_attach_scaling(
    cg_posterior_table *table,
    const char *mode
) {
    double *totals;
    size_t count = 0;
    size_t i;

    if (table == NULL || mode == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    if (mode[0] != '\0' &&
        strcmp(mode, "sparse-boost") != 0 &&
        strcmp(mode, "rich-boost") != 0) {
        return CG_ERR_UNSUPPORTED_OPTION;
    }
    free(table->scale_mode);
    table->scale_mode = cg_strdup(mode);
    if (table->scale_mode == NULL) {
        return CG_ERR_OOM;
    }
    table->median_row_total = 0.0;
    if (mode[0] == '\0' || table->row_len == 0) {
        return CG_OK;
    }
    totals = (double *)calloc(table->row_len, sizeof(*totals));
    if (totals == NULL) {
        return CG_ERR_OOM;
    }
    for (i = 0; i < table->row_len; i++) {
        if (table->row_total[i].count > 0) {
            totals[count++] = (double)table->row_total[i].count;
        }
    }
    if (count > 0) {
        qsort(totals, count, sizeof(*totals), double_cmp);
        table->median_row_total = totals[count / 2];
    }
    free(totals);
    return CG_OK;
}

static double scaling_factor(const cg_posterior_table *table, const char *source) {
    double n;
    double ratio;

    if (table->scale_mode == NULL || table->scale_mode[0] == '\0' || table->median_row_total <= 0.0) {
        return 1.0;
    }
    n = (double)total_get(table->row_total, table->row_len, source);
    if (n <= 0.0) {
        return 1.0;
    }
    if (strcmp(table->scale_mode, "sparse-boost") == 0) {
        ratio = table->median_row_total / n;
        return ratio < 1.0 ? 1.0 : ratio;
    }
    if (strcmp(table->scale_mode, "rich-boost") == 0) {
        ratio = n / table->median_row_total;
        return ratio < 1.0 ? 1.0 : ratio;
    }
    return 1.0;
}

cg_status cg_posterior_mean(
    const cg_posterior_table *table,
    const char *source,
    const char *target,
    double *out
) {
    double alpha = 0.0;
    double observed;
    double denom;
    double scale;
    cg_status status;

    if (table == NULL || source == NULL || target == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    status = cg_dirichlet_prior_alpha(table->prior, source, target, &alpha);
    if (status != CG_OK) {
        return status;
    }
    scale = scaling_factor(table, source);
    alpha *= scale;
    observed = (double)cg_posterior_table_count_pair(table, source, target);
    denom = table->prior->concentration * scale + (double)total_get(table->row_total, table->row_len, source);
    *out = denom <= 0.0 ? 0.0 : (alpha + observed) / denom;
    return CG_OK;
}

static double null_conditional(const cg_posterior_table *table, const char *target) {
    uint64_t count;

    if (table->columns > 0) {
        count = total_get(table->target_total, table->target_len, target);
        if (count > 0) {
            return (double)count / (double)table->columns;
        }
    }
    if (prior_inventory_index(table->prior, target) >= 0 && table->prior->count > 0) {
        return 1.0 / (double)table->prior->count;
    }
    return 0.0;
}

cg_status cg_posterior_log_odds(
    const cg_posterior_table *table,
    const char *source,
    const char *target,
    double *out
) {
    double post = 0.0;
    double null_value;
    cg_status status;

    if (table == NULL || source == NULL || target == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    status = cg_posterior_mean(table, source, target, &post);
    if (status != CG_OK) {
        return status;
    }
    null_value = null_conditional(table, target);
    if (post <= 0.0 || null_value <= 0.0) {
        *out = 0.0;
        return CG_OK;
    }
    *out = log(post / null_value) / log(2.0);
    return CG_OK;
}

cg_status cg_posterior_score(
    const cg_context *ctx,
    const cg_posterior_table *table,
    const char *source,
    const char *target,
    int prosody_aware,
    double *out
) {
    double lo = 0.0;
    cg_status status;

    if (ctx == NULL || table == NULL || source == NULL || target == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    status = cg_posterior_log_odds(table, source, target, &lo);
    if (status != CG_OK) {
        return status;
    }
    *out = tanh(lo / CG_POSTERIOR_SCORER_SCALE);
    if (prosody_aware && source[0] != '\0' && target[0] != '\0') {
        cg_role rs = CG_ROLE_UNKNOWN;
        cg_role rt = CG_ROLE_UNKNOWN;
        status = cg_context_grapheme_role(ctx, source, &rs);
        if (status == CG_ERR_MERKMAL) {
            rs = CG_ROLE_UNKNOWN;
        } else if (status != CG_OK) {
            return status;
        }
        status = cg_context_grapheme_role(ctx, target, &rt);
        if (status == CG_ERR_MERKMAL) {
            rt = CG_ROLE_UNKNOWN;
        } else if (status != CG_OK) {
            return status;
        }
        if (rs != CG_ROLE_UNKNOWN && rt != CG_ROLE_UNKNOWN && rs != rt) {
            *out -= CG_ROLE_MISMATCH_PENALTY;
        }
    }
    return CG_OK;
}

static cg_status posterior_score_cb(const char *source, const char *target, void *user, double *out) {
    cg_posterior_score_user *u = (cg_posterior_score_user *)user;

    return cg_posterior_score(u->ctx, u->table, source, target, u->prosody_aware, out);
}

static cg_status train_pass(
    const cg_context *ctx,
    const char *source_lect,
    const char *target_lect,
    const cg_form_pair *pairs,
    uint64_t pair_count,
    const cg_dirichlet_prior *prior,
    const cg_posterior_table *scoring_table,
    cg_posterior_table **out
) {
    cg_posterior_table *table = NULL;
    uint64_t i;
    cg_status status;

    status = cg_posterior_table_new(source_lect, target_lect, prior, &table);
    if (status != CG_OK) {
        return status;
    }
    for (i = 0; i < pair_count; i++) {
        cg_alignment *alignment = NULL;
        uint64_t j;

        if (pairs[i].source_count == 0 || pairs[i].target_count == 0) {
            continue;
        }
        if (scoring_table == NULL) {
            cg_prior_score_user user = {ctx, 1};
            status = cg_internal_needleman_wunsch(
                pairs[i].source,
                (size_t)pairs[i].source_count,
                pairs[i].target,
                (size_t)pairs[i].target_count,
                (cg_internal_score_fn)score_prior_cb,
                &user,
                &alignment
            );
        } else {
            cg_posterior_score_user user = {ctx, scoring_table, 1};
            status = cg_internal_needleman_wunsch(
                pairs[i].source,
                (size_t)pairs[i].source_count,
                pairs[i].target,
                (size_t)pairs[i].target_count,
                posterior_score_cb,
                &user,
                &alignment
            );
        }
        if (status != CG_OK) {
            cg_posterior_table_free(table);
            return status;
        }
        for (j = 0; j < cg_alignment_count(alignment); j++) {
            status = cg_posterior_table_add(
                table,
                cg_alignment_source_at(alignment, j),
                cg_alignment_target_at(alignment, j)
            );
            if (status != CG_OK) {
                cg_alignment_free(alignment);
                cg_posterior_table_free(table);
                return status;
            }
        }
        table->total_pairs++;
        cg_alignment_free(alignment);
    }
    *out = table;
    return CG_OK;
}

cg_status cg_train_posterior(
    const cg_context *ctx,
    const char *source_lect,
    const char *target_lect,
    const cg_form_pair *pairs,
    uint64_t pair_count,
    const cg_dirichlet_prior *prior,
    int iterations,
    cg_posterior_table **out
) {
    cg_posterior_table *table = NULL;
    int i;
    cg_status status;

    if (ctx == NULL || source_lect == NULL || target_lect == NULL || prior == NULL || out == NULL ||
        (pairs == NULL && pair_count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (iterations < 1) {
        iterations = 1;
    }
    for (i = 0; i < iterations; i++) {
        cg_posterior_table *next = NULL;
        status = train_pass(ctx, source_lect, target_lect, pairs, pair_count, prior, table, &next);
        if (status != CG_OK) {
            cg_posterior_table_free(table);
            return status;
        }
        cg_posterior_table_free(table);
        table = next;
    }
    *out = table;
    return CG_OK;
}
