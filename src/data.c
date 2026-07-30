#include "internal/data.h"

#include <stdlib.h>
#include <string.h>

char *cg_strdup(const char *s) {
    size_t len;
    char *out;

    if (s == NULL) {
        s = "";
    }
    len = strlen(s);
    out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, len + 1);
    return out;
}

char *cg_trim_owned(char *s) {
    char *start;
    char *end;

    if (s == NULL) {
        return NULL;
    }
    start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    return s;
}

void cg_string_free(char *value) {
    free(value);
}

void cg_free_record(cg_record *record) {
    if (record == NULL) {
        return;
    }
    free((char *)record->id);
    free((char *)record->language_id);
    free((char *)record->concept);
    free((char *)record->segments);
    free((char *)record->family);
    free((char *)record->cognacy);
    *record = (cg_record){0};
}

void cg_free_token(cg_token *token) {
    if (token == NULL) {
        return;
    }
    free((char *)token->grapheme);
    free((char *)token->tone);
    *token = (cg_token){0};
}

void cg_free_oov_row(cg_oov_row *row) {
    if (row == NULL) {
        return;
    }
    free((char *)row->grapheme);
    *row = (cg_oov_row){0};
}

void cg_free_sidecar_row(cg_sidecar_row *row) {
    if (row == NULL) {
        return;
    }
    free((char *)row->id);
    free((char *)row->cognator_id);
    *row = (cg_sidecar_row){0};
}

cg_status cg_sidecar_row_copy(const cg_sidecar_row *row, cg_sidecar_row *out) {
    if (row == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = *row;
    out->id = cg_strdup(row->id);
    out->cognator_id = cg_strdup(row->cognator_id);
    if (out->id == NULL || out->cognator_id == NULL) {
        cg_free_sidecar_row(out);
        return CG_ERR_OOM;
    }
    return CG_OK;
}

void cg_sidecar_row_clear(cg_sidecar_row *row) {
    cg_free_sidecar_row(row);
}

cg_status cg_oov_row_copy(const cg_oov_row *row, cg_oov_row *out) {
    if (row == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = *row;
    out->grapheme = cg_strdup(row->grapheme);
    if (out->grapheme == NULL) {
        cg_free_oov_row(out);
        return CG_ERR_OOM;
    }
    return CG_OK;
}

void cg_oov_row_clear(cg_oov_row *row) {
    cg_free_oov_row(row);
}
