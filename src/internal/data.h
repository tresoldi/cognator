#ifndef COGNATOR_INTERNAL_DATA_H
#define COGNATOR_INTERNAL_DATA_H

#include "cognator.h"

#include <stddef.h>

struct cg_records {
    cg_record *items;
    size_t count;
    size_t cap;
};

struct cg_tokens {
    cg_token *items;
    size_t count;
    size_t cap;
};

struct cg_oov_report {
    cg_oov_row *items;
    size_t count;
    size_t cap;
};

struct cg_result {
    cg_sidecar_row *rows;
    size_t row_count;
    size_t row_cap;
    double threshold;
    cg_oov_row *oov;
    size_t oov_count;
    /* Pairwise export (cg_detect_with_pairs); all zero otherwise. */
    cg_pair_row *pairs;
    size_t pair_count;
    int has_pairs;
    cg_pairs_summary pairs_summary;
    /* One per sidecar row when morpheme clusters were computed, else NULL. */
    char **morpheme_ids;
};

char *cg_strdup(const char *s);
char *cg_trim_owned(char *s);
void cg_free_record(cg_record *record);
void cg_free_token(cg_token *token);
void cg_free_oov_row(cg_oov_row *row);
void cg_free_sidecar_row(cg_sidecar_row *row);
void cg_free_pair_row(cg_pair_row *row);
cg_status cg_internal_write_sidecar_rows(
    const char *path,
    const cg_sidecar_row *rows,
    uint64_t count,
    const char *extra_header,
    const char *const *extra_values
);

#endif
