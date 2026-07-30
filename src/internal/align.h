#ifndef COGNATOR_INTERNAL_ALIGN_H
#define COGNATOR_INTERNAL_ALIGN_H

#include "cognator.h"

#include <stddef.h>

typedef cg_status (*cg_internal_score_fn)(
    const char *source,
    const char *target,
    void *user,
    double *out
);

cg_status cg_internal_needleman_wunsch(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_internal_score_fn score,
    void *user,
    cg_alignment **out
);

cg_status cg_internal_distance(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_internal_score_fn score,
    void *user,
    double *out
);

cg_status cg_internal_distance_combined(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_internal_score_fn score,
    void *user,
    double *out
);

#endif
