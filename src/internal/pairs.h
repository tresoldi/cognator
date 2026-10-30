#ifndef COGNATOR_INTERNAL_PAIRS_H
#define COGNATOR_INTERNAL_PAIRS_H

#include "cognator.h"
#include "internal/align.h"

#include <stddef.h>

/* One input row as the pipeline sees it: its segment sequence (boundaries
 * removed, class reduction applied) and the offsets in that sequence where a
 * `+` stood. `breaks` may be NULL when break_count is 0. */
typedef struct cg_pairs_form {
    const char *const *items;
    size_t count;
    const size_t *breaks;
    size_t break_count;
} cg_pairs_form;

/* The distance the pipeline would compute between two spans of rows rec_a and
 * rec_b: detect.c owns the per-lect-pair scorers and the alignment mode, so it
 * supplies this and pairs.c never sees them. `distance` is the number
 * clustering uses (global or combined, per alignment mode). */
typedef cg_status (*cg_pairs_distance_fn)(
    void *user,
    size_t rec_a,
    const char *const *a,
    size_t a_count,
    size_t rec_b,
    const char *const *b,
    size_t b_count,
    cg_internal_components *components,
    double *distance
);

typedef struct cg_pairs_input {
    const cg_records *records;
    const cg_pairs_form *forms;          /* one per record */
    cg_pairs_distance_fn distance;
    void *user;
    cg_pairs_spec spec;                  /* already defaulted */
    double word_threshold;               /* result->threshold */
    double fixed_threshold;              /* options.threshold; > 0 means fixed */
    double threshold_min;
    double threshold_max;
    cg_graph_spec graph_spec;
    cg_refine_spec refine_spec;
    uint64_t seed;                       /* options.seed, for Infomap */
} cg_pairs_input;

/* Fills result->pairs, result->pairs_summary and, when asked, morpheme_ids. */
cg_status cg_internal_pairs_run(const cg_pairs_input *in, cg_result *result);

/* Fills in the documented defaults for zero/negative fields. */
void cg_internal_pairs_spec_effective(cg_pairs_spec *dst, const cg_pairs_spec *src);

#endif
