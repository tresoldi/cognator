#include "internal/data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cg_fields {
    char **items;
    size_t count;
    size_t cap;
} cg_fields;

typedef struct cg_columns {
    int id;
    int language;
    int concept;
    int segments;
    int family;
    int cognacy;
    int formid;
} cg_columns;

static void fields_free(cg_fields *fields) {
    size_t i;

    if (fields == NULL) {
        return;
    }
    for (i = 0; i < fields->count; i++) {
        free(fields->items[i]);
    }
    free(fields->items);
    *fields = (cg_fields){0};
}

static cg_status fields_push(cg_fields *fields, const char *text, size_t len) {
    char **next;
    char *copy;

    if (fields->count == fields->cap) {
        size_t next_cap = fields->cap == 0 ? 8 : fields->cap * 2;
        next = (char **)realloc(fields->items, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        fields->items = next;
        fields->cap = next_cap;
    }
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return CG_ERR_OOM;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';
    cg_trim_owned(copy);
    fields->items[fields->count++] = copy;
    return CG_OK;
}

static cg_status parse_line(const char *line, char delimiter, cg_fields *out) {
    size_t len;
    size_t i = 0;
    char *field = NULL;
    size_t field_len = 0;
    size_t field_cap = 0;
    int quoted = 0;

    *out = (cg_fields){0};
    len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        len--;
    }
    while (i <= len) {
        char ch = i < len ? line[i] : delimiter;
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < len && line[i + 1] == '"') {
                    ch = '"';
                    i++;
                } else {
                    quoted = 0;
                    i++;
                    continue;
                }
            }
        } else if (ch == '"') {
            quoted = 1;
            i++;
            continue;
        } else if (ch == delimiter || i == len) {
            cg_status status = fields_push(out, field == NULL ? "" : field, field_len);
            free(field);
            field = NULL;
            field_len = 0;
            field_cap = 0;
            if (status != CG_OK) {
                fields_free(out);
                return status;
            }
            i++;
            continue;
        }
        if (field_len + 1 >= field_cap) {
            size_t next_cap = field_cap == 0 ? 32 : field_cap * 2;
            char *next = (char *)realloc(field, next_cap);
            if (next == NULL) {
                free(field);
                fields_free(out);
                return CG_ERR_OOM;
            }
            field = next;
            field_cap = next_cap;
        }
        field[field_len++] = ch;
        i++;
    }
    free(field);
    return quoted ? CG_ERR_PARSE : CG_OK;
}

static char delimiter_for(const char *path) {
    const char *dot = strrchr(path, '.');

    if (dot != NULL && strcmp(dot, ".csv") == 0) {
        return ',';
    }
    return '\t';
}

static int column_pick(const cg_fields *header, const char *const *names, size_t count) {
    size_t i;
    size_t j;

    for (j = 0; j < count; j++) {
        for (i = 0; i < header->count; i++) {
            if (strcmp(header->items[i], names[j]) == 0) {
                return (int)i;
            }
        }
    }
    return -1;
}

static cg_status resolve_columns(const cg_fields *header, cg_columns *cols) {
    static const char *const id_names[] = {"ID"};
    static const char *const language_names[] = {"Language_ID", "av_id"};
    static const char *const concept_names[] = {"Concept", "Concepticon_Gloss", "Parameter_ID", "concept_id", "concept_label"};
    static const char *const segment_names[] = {"Segments"};
    static const char *const family_names[] = {"Family", "Dataset"};
    static const char *const cognacy_names[] = {"Cognacy"};
    static const char *const formid_names[] = {"source_form_id"};

    *cols = (cg_columns){-1, -1, -1, -1, -1, -1, -1};
    cols->id = column_pick(header, id_names, 1);
    cols->language = column_pick(header, language_names, 2);
    cols->concept = column_pick(header, concept_names, 5);
    cols->segments = column_pick(header, segment_names, 1);
    cols->family = column_pick(header, family_names, 2);
    cols->cognacy = column_pick(header, cognacy_names, 1);
    cols->formid = column_pick(header, formid_names, 1);
    if (cols->language < 0 || cols->concept < 0 || cols->segments < 0) {
        return CG_ERR_PARSE;
    }
    return CG_OK;
}

static const char *field_get(const cg_fields *fields, int index) {
    if (index < 0 || (size_t)index >= fields->count) {
        return "";
    }
    return fields->items[index];
}

static char *first_cognacy(const char *value) {
    const char *semi;
    char *out;
    size_t len;

    semi = strchr(value, ';');
    len = semi == NULL ? strlen(value) : (size_t)(semi - value);
    out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, value, len);
    out[len] = '\0';
    cg_trim_owned(out);
    return out;
}

static char *synthesized_id(const char *language, const char *formid, unsigned long line) {
    size_t len = strlen(language) + strlen(formid) + 32;
    char *out = (char *)malloc(len);

    if (out == NULL) {
        return NULL;
    }
    if (formid[0] != '\0') {
        snprintf(out, len, "%s::%s", language, formid);
    } else {
        snprintf(out, len, "%s::r%lu", language, line);
    }
    return out;
}

static cg_status records_push(cg_records *records, cg_record record) {
    cg_record *next;

    if (records->count == records->cap) {
        size_t next_cap = records->cap == 0 ? 64 : records->cap * 2;
        next = (cg_record *)realloc(records->items, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        records->items = next;
        records->cap = next_cap;
    }
    records->items[records->count++] = record;
    return CG_OK;
}

static cg_status record_clone(const cg_record *src, cg_record *out) {
    cg_record record = {0};
    record.id = cg_strdup(src->id);
    record.language_id = cg_strdup(src->language_id);
    record.concept = cg_strdup(src->concept);
    record.segments = cg_strdup(src->segments);
    record.family = cg_strdup(src->family);
    record.cognacy = cg_strdup(src->cognacy);
    if (record.id == NULL || record.language_id == NULL || record.concept == NULL ||
        record.segments == NULL || record.family == NULL || record.cognacy == NULL) {
        cg_free_record(&record);
        return CG_ERR_OOM;
    }
    *out = record;
    return CG_OK;
}

static cg_status extract_record(const cg_fields *fields, const cg_columns *cols, unsigned long line, cg_record *out) {
    const char *language = field_get(fields, cols->language);
    const char *id = field_get(fields, cols->id);
    const char *segments = field_get(fields, cols->segments);
    cg_record record = {0};

    if (language[0] == '\0' || segments[0] == '\0') {
        *out = (cg_record){0};
        return CG_OK;
    }
    record.language_id = cg_strdup(language);
    record.concept = cg_strdup(field_get(fields, cols->concept));
    record.segments = cg_strdup(segments);
    record.family = cg_strdup(field_get(fields, cols->family));
    if (field_get(fields, cols->cognacy)[0] != '\0') {
        record.cognacy = first_cognacy(field_get(fields, cols->cognacy));
    } else {
        record.cognacy = cg_strdup("");
    }
    if (id[0] != '\0') {
        record.id = cg_strdup(id);
    } else {
        record.id = synthesized_id(language, field_get(fields, cols->formid), line);
    }
    if (record.id == NULL || record.language_id == NULL || record.concept == NULL ||
        record.segments == NULL || record.family == NULL || record.cognacy == NULL) {
        cg_free_record(&record);
        return CG_ERR_OOM;
    }
    *out = record;
    return CG_OK;
}

cg_status cg_read_table(const char *path, cg_records **out) {
    FILE *f;
    char line[65536];
    char delimiter;
    cg_fields header;
    cg_columns cols;
    cg_records *records;
    unsigned long line_no = 1;
    cg_status status;

    if (path == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    f = fopen(path, "rb");
    if (f == NULL) {
        return CG_ERR_IO;
    }
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        return CG_ERR_PARSE;
    }
    delimiter = delimiter_for(path);
    status = parse_line(line, delimiter, &header);
    if (status != CG_OK) {
        fclose(f);
        return status;
    }
    status = resolve_columns(&header, &cols);
    fields_free(&header);
    if (status != CG_OK) {
        fclose(f);
        return status;
    }
    records = (cg_records *)calloc(1, sizeof(*records));
    if (records == NULL) {
        fclose(f);
        return CG_ERR_OOM;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        cg_fields fields;
        cg_record record = {0};

        line_no++;
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') {
            continue;
        }
        status = parse_line(line, delimiter, &fields);
        if (status != CG_OK) {
            cg_records_free(records);
            fclose(f);
            return status;
        }
        status = extract_record(&fields, &cols, line_no, &record);
        fields_free(&fields);
        if (status != CG_OK) {
            cg_records_free(records);
            fclose(f);
            return status;
        }
        if (record.id != NULL && record.language_id != NULL && record.segments != NULL) {
            status = records_push(records, record);
            if (status != CG_OK) {
                cg_free_record(&record);
                cg_records_free(records);
                fclose(f);
                return status;
            }
        }
    }
    if (ferror(f)) {
        cg_records_free(records);
        fclose(f);
        return CG_ERR_IO;
    }
    fclose(f);
    *out = records;
    return CG_OK;
}

void cg_records_free(cg_records *records) {
    size_t i;

    if (records == NULL) {
        return;
    }
    for (i = 0; i < records->count; i++) {
        cg_free_record(&records->items[i]);
    }
    free(records->items);
    free(records);
}

uint64_t cg_records_count(const cg_records *records) {
    return records == NULL ? 0 : (uint64_t)records->count;
}

const cg_record *cg_records_get(const cg_records *records, uint64_t index) {
    if (records == NULL || index >= records->count) {
        return NULL;
    }
    return &records->items[index];
}

cg_status cg_records_filter_family(
    const cg_records *records,
    const char *family,
    cg_records **out
) {
    cg_records *filtered;
    size_t i;
    cg_status status;

    if (records == NULL || family == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    filtered = (cg_records *)calloc(1, sizeof(*filtered));
    if (filtered == NULL) {
        return CG_ERR_OOM;
    }
    for (i = 0; i < records->count; i++) {
        cg_record record = {0};
        if (strcmp(records->items[i].family == NULL ? "" : records->items[i].family, family) != 0) {
            continue;
        }
        status = record_clone(&records->items[i], &record);
        if (status == CG_OK) {
            status = records_push(filtered, record);
        }
        if (status != CG_OK) {
            cg_free_record(&record);
            cg_records_free(filtered);
            return status;
        }
    }
    *out = filtered;
    return CG_OK;
}

cg_status cg_write_sidecar_rows(
    const char *path,
    const cg_sidecar_row *rows,
    uint64_t count
) {
    FILE *f;
    uint64_t i;

    if (path == NULL || (rows == NULL && count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return CG_ERR_IO;
    }
    if (fprintf(
        f,
        "ID\tCognator_ID\tCognator_Confidence\tCognator_Assigned_Support\t"
        "Cognator_Alternative_Support\tCognator_Support_Margin\t"
        "Cognator_Cluster_Size\tCognator_Candidate_Neighbors\t"
        "Cognator_Bootstrap\tCognator_Bootstrap_Low\tCognator_Bootstrap_High\n"
    ) < 0) {
        fclose(f);
        return CG_ERR_IO;
    }
    for (i = 0; i < count; i++) {
        const cg_sidecar_row *row = &rows[i];
        if (fprintf(
            f,
            "%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%d\t%d\t%.6f\t%.6f\t%.6f\n",
            row->id == NULL ? "" : row->id,
            row->cognator_id == NULL ? "" : row->cognator_id,
            row->confidence,
            row->assigned_support,
            row->alternative_support,
            row->support_margin,
            row->cluster_size,
            row->candidate_neighbors,
            row->bootstrap,
            row->bootstrap_low,
            row->bootstrap_high
        ) < 0) {
            fclose(f);
            return CG_ERR_IO;
        }
    }
    return fclose(f) == 0 ? CG_OK : CG_ERR_IO;
}
