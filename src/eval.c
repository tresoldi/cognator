#include "internal/data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cg_assignment {
    char *id;
    char *cluster;
} cg_assignment;

typedef struct cg_assignments {
    cg_assignment *items;
    size_t count;
    size_t cap;
} cg_assignments;

typedef struct cg_eval_fields {
    char **items;
    size_t count;
    size_t cap;
} cg_eval_fields;

static void eval_fields_free(cg_eval_fields *fields) {
    size_t i;
    if (fields == NULL) return;
    for (i = 0; i < fields->count; i++) free(fields->items[i]);
    free(fields->items);
    *fields = (cg_eval_fields){0};
}

static cg_status eval_fields_push(cg_eval_fields *fields, const char *text, size_t len) {
    char **next;
    char *copy;
    if (fields->count == fields->cap) {
        size_t next_cap = fields->cap == 0 ? 8 : fields->cap * 2;
        next = (char **)realloc(fields->items, next_cap * sizeof(*next));
        if (next == NULL) return CG_ERR_OOM;
        fields->items = next;
        fields->cap = next_cap;
    }
    copy = (char *)malloc(len + 1);
    if (copy == NULL) return CG_ERR_OOM;
    memcpy(copy, text, len);
    copy[len] = '\0';
    cg_trim_owned(copy);
    fields->items[fields->count++] = copy;
    return CG_OK;
}

static cg_status eval_parse_tsv_line(const char *line, cg_eval_fields *out) {
    size_t len;
    size_t start = 0;
    size_t i;
    *out = (cg_eval_fields){0};
    len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    for (i = 0; i <= len; i++) {
        if (i == len || line[i] == '\t') {
            cg_status status = eval_fields_push(out, line + start, i - start);
            if (status != CG_OK) {
                eval_fields_free(out);
                return status;
            }
            start = i + 1;
        }
    }
    return CG_OK;
}

static int eval_column_index(const cg_eval_fields *header, const char *name) {
    size_t i;
    for (i = 0; i < header->count; i++) {
        if (strcmp(header->items[i], name) == 0) return (int)i;
    }
    return -1;
}

static const char *eval_field_get(const cg_eval_fields *fields, int index) {
    if (index < 0 || (size_t)index >= fields->count) return "";
    return fields->items[index];
}

static void assignment_free(cg_assignment *item) {
    if (item == NULL) return;
    free(item->id);
    free(item->cluster);
    *item = (cg_assignment){0};
}

static void assignments_free(cg_assignments *items) {
    size_t i;
    if (items == NULL) return;
    for (i = 0; i < items->count; i++) assignment_free(&items->items[i]);
    free(items->items);
    *items = (cg_assignments){0};
}

static size_t assignment_find(const cg_assignments *items, const char *id) {
    size_t i;
    for (i = 0; i < items->count; i++) {
        if (strcmp(items->items[i].id, id) == 0) return i;
    }
    return SIZE_MAX;
}

static cg_status assignments_set(cg_assignments *items, const char *id, const char *cluster) {
    size_t idx;
    char *id_copy;
    char *cluster_copy;
    cg_assignment *next;
    if (id == NULL || cluster == NULL || id[0] == '\0' || cluster[0] == '\0') return CG_OK;
    idx = assignment_find(items, id);
    if (idx != SIZE_MAX) {
        cluster_copy = cg_strdup(cluster);
        if (cluster_copy == NULL) return CG_ERR_OOM;
        free(items->items[idx].cluster);
        items->items[idx].cluster = cluster_copy;
        return CG_OK;
    }
    if (items->count == items->cap) {
        size_t next_cap = items->cap == 0 ? 64 : items->cap * 2;
        next = (cg_assignment *)realloc(items->items, next_cap * sizeof(*next));
        if (next == NULL) return CG_ERR_OOM;
        items->items = next;
        items->cap = next_cap;
    }
    id_copy = cg_strdup(id);
    cluster_copy = cg_strdup(cluster);
    if (id_copy == NULL || cluster_copy == NULL) {
        free(id_copy);
        free(cluster_copy);
        return CG_ERR_OOM;
    }
    items->items[items->count++] = (cg_assignment){id_copy, cluster_copy};
    return CG_OK;
}

static const char *assignment_cluster(const cg_assignments *items, const char *id) {
    size_t idx = assignment_find(items, id);
    return idx == SIZE_MAX ? NULL : items->items[idx].cluster;
}

static cg_status read_sidecar_pred(const char *path, cg_assignments *out) {
    FILE *f;
    char line[65536];
    cg_eval_fields header = {0};
    int id_col;
    int cluster_col;
    cg_status status;

    *out = (cg_assignments){0};
    f = fopen(path, "rb");
    if (f == NULL) return CG_ERR_IO;
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        return CG_ERR_PARSE;
    }
    status = eval_parse_tsv_line(line, &header);
    if (status != CG_OK) {
        fclose(f);
        return status;
    }
    id_col = eval_column_index(&header, "ID");
    cluster_col = eval_column_index(&header, "Cognator_ID");
    eval_fields_free(&header);
    if (id_col < 0 || cluster_col < 0) {
        fclose(f);
        return CG_ERR_PARSE;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        cg_eval_fields fields = {0};
        status = eval_parse_tsv_line(line, &fields);
        if (status == CG_OK) {
            status = assignments_set(out, eval_field_get(&fields, id_col), eval_field_get(&fields, cluster_col));
        }
        eval_fields_free(&fields);
        if (status != CG_OK) {
            assignments_free(out);
            fclose(f);
            return status;
        }
    }
    if (ferror(f)) {
        assignments_free(out);
        fclose(f);
        return CG_ERR_IO;
    }
    return fclose(f) == 0 ? CG_OK : CG_ERR_IO;
}

static cg_status make_truth_cluster(const cg_record *record, int per_concept, char **out) {
    size_t len;
    char *cluster;
    if (!per_concept) {
        cluster = cg_strdup(record->cognacy);
        if (cluster == NULL) return CG_ERR_OOM;
        *out = cluster;
        return CG_OK;
    }
    len = strlen(record->concept == NULL ? "" : record->concept) + strlen(record->cognacy == NULL ? "" : record->cognacy) + 2;
    cluster = (char *)malloc(len);
    if (cluster == NULL) return CG_ERR_OOM;
    snprintf(cluster, len, "%s:%s", record->concept == NULL ? "" : record->concept, record->cognacy == NULL ? "" : record->cognacy);
    *out = cluster;
    return CG_OK;
}

static cg_status read_truth_records(const char *path, const char *truth_col, int per_concept, cg_assignments *out) {
    cg_records *records = NULL;
    uint64_t i;
    cg_status status;
    if (truth_col == NULL) truth_col = "Cognacy";
    if (strcmp(truth_col, "Cognacy") != 0) return CG_ERR_UNSUPPORTED_OPTION;
    *out = (cg_assignments){0};
    status = cg_read_table(path, &records);
    if (status != CG_OK) return status;
    for (i = 0; i < cg_records_count(records); i++) {
        const cg_record *record = cg_records_get(records, i);
        char *cluster = NULL;
        if (record == NULL || record->cognacy == NULL || record->cognacy[0] == '\0') continue;
        status = make_truth_cluster(record, per_concept, &cluster);
        if (status == CG_OK) status = assignments_set(out, record->id, cluster);
        free(cluster);
        if (status != CG_OK) {
            assignments_free(out);
            cg_records_free(records);
            return status;
        }
    }
    cg_records_free(records);
    return CG_OK;
}

static cg_status read_truth_assignments(const cg_records *records, const char *truth_col, int per_concept, cg_assignments *out) {
    uint64_t i;
    cg_status status = CG_OK;
    if (records == NULL) return CG_ERR_INVALID_ARGUMENT;
    if (truth_col == NULL) truth_col = "Cognacy";
    if (strcmp(truth_col, "Cognacy") != 0) return CG_ERR_UNSUPPORTED_OPTION;
    *out = (cg_assignments){0};
    for (i = 0; i < cg_records_count(records); i++) {
        const cg_record *record = cg_records_get(records, i);
        char *cluster = NULL;
        if (record == NULL || record->cognacy == NULL || record->cognacy[0] == '\0') continue;
        status = make_truth_cluster(record, per_concept, &cluster);
        if (status == CG_OK) status = assignments_set(out, record->id, cluster);
        free(cluster);
        if (status != CG_OK) {
            assignments_free(out);
            return status;
        }
    }
    return CG_OK;
}

static cg_status read_result_pred(const cg_result *result, cg_assignments *out) {
    uint64_t i;
    cg_status status = CG_OK;
    if (result == NULL) return CG_ERR_INVALID_ARGUMENT;
    *out = (cg_assignments){0};
    for (i = 0; i < cg_result_row_count(result); i++) {
        const cg_sidecar_row *row = cg_result_row_at(result, i);
        if (row == NULL) continue;
        status = assignments_set(out, row->id, row->cognator_id);
        if (status != CG_OK) {
            assignments_free(out);
            return status;
        }
    }
    return CG_OK;
}

static size_t pred_cluster_size_for_truth(const cg_assignments *pred, const cg_assignments *truth, const char *pred_cluster) {
    size_t i;
    size_t count = 0;
    for (i = 0; i < pred->count; i++) {
        if (strcmp(pred->items[i].cluster, pred_cluster) == 0 && assignment_cluster(truth, pred->items[i].id) != NULL) {
            count++;
        }
    }
    return count;
}

static size_t truth_cluster_size(const cg_assignments *truth, const char *truth_cluster) {
    size_t i;
    size_t count = 0;
    for (i = 0; i < truth->count; i++) {
        if (strcmp(truth->items[i].cluster, truth_cluster) == 0) count++;
    }
    return count;
}

static size_t shared_cluster_size(const cg_assignments *pred, const cg_assignments *truth, const char *pred_cluster, const char *truth_cluster) {
    size_t i;
    size_t count = 0;
    for (i = 0; i < pred->count; i++) {
        const char *t_cluster;
        if (strcmp(pred->items[i].cluster, pred_cluster) != 0) continue;
        t_cluster = assignment_cluster(truth, pred->items[i].id);
        if (t_cluster != NULL && strcmp(t_cluster, truth_cluster) == 0) count++;
    }
    return count;
}

static void compute_b_cubed(const cg_assignments *pred, const cg_assignments *truth, cg_eval_result *out) {
    size_t i;
    double p_sum = 0.0;
    double r_sum = 0.0;
    uint64_t scored = 0;
    memset(out, 0, sizeof(*out));
    out->pred_count = (uint64_t)pred->count;
    out->truth_count = (uint64_t)truth->count;
    for (i = 0; i < pred->count; i++) {
        const char *truth_cluster = assignment_cluster(truth, pred->items[i].id);
        size_t pm;
        size_t tm;
        size_t shared;
        if (truth_cluster == NULL) continue;
        pm = pred_cluster_size_for_truth(pred, truth, pred->items[i].cluster);
        tm = truth_cluster_size(truth, truth_cluster);
        shared = shared_cluster_size(pred, truth, pred->items[i].cluster, truth_cluster);
        if (pm == 0 || tm == 0) continue;
        p_sum += (double)shared / (double)pm;
        r_sum += (double)shared / (double)tm;
        scored++;
    }
    out->scored_count = scored;
    if (scored == 0) return;
    out->precision = p_sum / (double)scored;
    out->recall = r_sum / (double)scored;
    if (out->precision + out->recall > 0.0) {
        out->f1 = 2.0 * out->precision * out->recall / (out->precision + out->recall);
    }
}

cg_status cg_eval_b_cubed(
    const char *pred_path,
    const char *truth_path,
    const char *truth_col,
    int per_concept,
    cg_eval_result *out
) {
    cg_assignments pred = {0};
    cg_assignments truth = {0};
    cg_status status;
    if (pred_path == NULL || truth_path == NULL || out == NULL) return CG_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    status = read_sidecar_pred(pred_path, &pred);
    if (status == CG_OK) status = read_truth_records(truth_path, truth_col, per_concept, &truth);
    if (status == CG_OK) compute_b_cubed(&pred, &truth, out);
    assignments_free(&pred);
    assignments_free(&truth);
    return status;
}

cg_status cg_eval_b_cubed_result(
    const cg_result *pred_result,
    const cg_records *truth_records,
    const char *truth_col,
    int per_concept,
    cg_eval_result *out
) {
    cg_assignments pred = {0};
    cg_assignments truth = {0};
    cg_status status;
    if (pred_result == NULL || truth_records == NULL || out == NULL) return CG_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    status = read_result_pred(pred_result, &pred);
    if (status == CG_OK) status = read_truth_assignments(truth_records, truth_col, per_concept, &truth);
    if (status == CG_OK) compute_b_cubed(&pred, &truth, out);
    assignments_free(&pred);
    assignments_free(&truth);
    return status;
}
