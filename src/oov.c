#include "internal/data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cg_status oov_insert(cg_oov_report *report, const char *grapheme, size_t index) {
    cg_oov_row *next;
    size_t i;

    if (report->count == report->cap) {
        size_t next_cap = report->cap == 0 ? 16 : report->cap * 2;
        next = (cg_oov_row *)realloc(report->items, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        report->items = next;
        report->cap = next_cap;
    }
    for (i = report->count; i > index; i--) {
        report->items[i] = report->items[i - 1];
    }
    report->items[index].grapheme = cg_strdup(grapheme);
    if (report->items[index].grapheme == NULL) {
        return CG_ERR_OOM;
    }
    report->items[index].count = 1;
    report->count++;
    return CG_OK;
}

cg_status cg_oov_report_new(cg_oov_report **out) {
    if (out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = (cg_oov_report *)calloc(1, sizeof(**out));
    return *out == NULL ? CG_ERR_OOM : CG_OK;
}

void cg_oov_report_free(cg_oov_report *report) {
    size_t i;

    if (report == NULL) {
        return;
    }
    for (i = 0; i < report->count; i++) {
        cg_free_oov_row(&report->items[i]);
    }
    free(report->items);
    free(report);
}

cg_status cg_oov_report_add(cg_oov_report *report, const char *grapheme) {
    size_t i;

    if (report == NULL || grapheme == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    for (i = 0; i < report->count; i++) {
        int cmp = strcmp(report->items[i].grapheme, grapheme);
        if (cmp == 0) {
            report->items[i].count++;
            return CG_OK;
        }
        if (cmp > 0) {
            return oov_insert(report, grapheme, i);
        }
    }
    return oov_insert(report, grapheme, report->count);
}

uint64_t cg_oov_report_count(const cg_oov_report *report) {
    return report == NULL ? 0 : (uint64_t)report->count;
}

const cg_oov_row *cg_oov_report_get(const cg_oov_report *report, uint64_t index) {
    if (report == NULL || index >= report->count) {
        return NULL;
    }
    return &report->items[index];
}

cg_status cg_write_oov_report_rows(
    const char *path,
    const cg_oov_row *rows,
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
    if (fprintf(f, "grapheme\tcount\n") < 0) {
        fclose(f);
        return CG_ERR_IO;
    }
    for (i = 0; i < count; i++) {
        if (fprintf(f, "%s\t%llu\n", rows[i].grapheme, (unsigned long long)rows[i].count) < 0) {
            fclose(f);
            return CG_ERR_IO;
        }
    }
    return fclose(f) == 0 ? CG_OK : CG_ERR_IO;
}
