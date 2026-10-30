#include "internal/align.h"
#include "internal/context.h"
#include "internal/data.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CG_DEFAULT_GAP_COST 0.5
#define CG_ROLE_MISMATCH_PENALTY 0.3

typedef cg_status (*cg_score_fn)(
    const char *source,
    const char *target,
    void *user,
    double *out
);

struct cg_alignment {
    char **source;
    char **target;
    size_t count;
    double score;
};

typedef struct cg_prior_score_user {
    const cg_context *ctx;
    int prosody_aware;
} cg_prior_score_user;

static double max3(double a, double b, double c) {
    double m = a;

    if (b > m) {
        m = b;
    }
    if (c > m) {
        m = c;
    }
    return m;
}

static cg_status score_prior_cb(const char *source, const char *target, void *user, double *out) {
    cg_prior_score_user *u = (cg_prior_score_user *)user;

    return cg_prior_score(u->ctx, source, target, u->prosody_aware, out);
}

cg_status cg_prior_score(
    const cg_context *ctx,
    const char *source,
    const char *target,
    int prosody_aware,
    double *out
) {
    double distance = 0.0;
    cg_status status;

    if (ctx == NULL || source == NULL || target == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    if (source[0] == '\0' || target[0] == '\0') {
        *out = -CG_DEFAULT_GAP_COST;
        return CG_OK;
    }
    status = cg_context_segment_distance(ctx, source, target, &distance);
    if (status == CG_ERR_MERKMAL) {
        distance = 1.0;
    } else if (status != CG_OK) {
        return status;
    }
    *out = 1.0 - 2.0 * distance;
    if (prosody_aware) {
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

static void alignment_clear(cg_alignment *alignment) {
    size_t i;

    if (alignment == NULL) {
        return;
    }
    for (i = 0; i < alignment->count; i++) {
        free(alignment->source[i]);
        free(alignment->target[i]);
    }
    free(alignment->source);
    free(alignment->target);
    *alignment = (cg_alignment){0};
}

static cg_status alignment_prepend(cg_alignment *alignment, const char *source, const char *target) {
    char **next_source;
    char **next_target;
    size_t bytes = (alignment->count + 1) * sizeof(*alignment->source);

    next_source = (char **)realloc(alignment->source, bytes);
    if (next_source == NULL) {
        return CG_ERR_OOM;
    }
    alignment->source = next_source;
    next_target = (char **)realloc(alignment->target, bytes);
    if (next_target == NULL) {
        return CG_ERR_OOM;
    }
    alignment->target = next_target;
    if (alignment->count > 0) {
        memmove(alignment->source + 1, alignment->source, alignment->count * sizeof(*alignment->source));
        memmove(alignment->target + 1, alignment->target, alignment->count * sizeof(*alignment->target));
    }
    alignment->source[0] = cg_strdup(source == NULL ? "" : source);
    alignment->target[0] = cg_strdup(target == NULL ? "" : target);
    if (alignment->source[0] == NULL || alignment->target[0] == NULL) {
        free(alignment->source[0]);
        free(alignment->target[0]);
        return CG_ERR_OOM;
    }
    alignment->count++;
    return CG_OK;
}

static cg_status score_cell(
    const char *source,
    const char *target,
    cg_score_fn score,
    void *user,
    double *out
) {
    return score(source, target, user, out);
}

static cg_status needleman_wunsch_generic(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    cg_alignment **out
) {
    size_t m = source_count;
    size_t n = target_count;
    size_t cols = n + 1;
    double *dp;
    cg_alignment *alignment;
    size_t i;
    size_t j;
    cg_status status = CG_OK;

    if (out == NULL || (source == NULL && source_count > 0) || (target == NULL && target_count > 0) || score == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    dp = (double *)calloc((m + 1) * (n + 1), sizeof(*dp));
    if (dp == NULL) {
        return CG_ERR_OOM;
    }
    for (i = 1; i <= m; i++) {
        dp[i * cols] = dp[(i - 1) * cols] - CG_DEFAULT_GAP_COST;
    }
    for (j = 1; j <= n; j++) {
        dp[j] = dp[j - 1] - CG_DEFAULT_GAP_COST;
    }
    for (i = 1; i <= m; i++) {
        for (j = 1; j <= n; j++) {
            double s;
            status = score_cell(source[i - 1], target[j - 1], score, user, &s);
            if (status != CG_OK) {
                free(dp);
                return status;
            }
            dp[i * cols + j] = max3(
                dp[(i - 1) * cols + (j - 1)] + s,
                dp[(i - 1) * cols + j] - CG_DEFAULT_GAP_COST,
                dp[i * cols + (j - 1)] - CG_DEFAULT_GAP_COST
            );
        }
    }
    alignment = (cg_alignment *)calloc(1, sizeof(*alignment));
    if (alignment == NULL) {
        free(dp);
        return CG_ERR_OOM;
    }
    alignment->score = dp[m * cols + n];
    i = m;
    j = n;
    while (i > 0 || j > 0) {
        double here = dp[i * cols + j];
        double s = 0.0;

        if (i > 0 && j > 0) {
            status = score_cell(source[i - 1], target[j - 1], score, user, &s);
            if (status != CG_OK) {
                alignment_clear(alignment);
                free(alignment);
                free(dp);
                return status;
            }
            if (fabs(here - (dp[(i - 1) * cols + (j - 1)] + s)) < 1e-9) {
                status = alignment_prepend(alignment, source[i - 1], target[j - 1]);
                i--;
                j--;
            } else if (i > 0 && fabs(here - (dp[(i - 1) * cols + j] - CG_DEFAULT_GAP_COST)) < 1e-9) {
                status = alignment_prepend(alignment, source[i - 1], "");
                i--;
            } else {
                status = alignment_prepend(alignment, "", target[j - 1]);
                j--;
            }
        } else if (i > 0) {
            status = alignment_prepend(alignment, source[i - 1], "");
            i--;
        } else {
            status = alignment_prepend(alignment, "", target[j - 1]);
            j--;
        }
        if (status != CG_OK) {
            alignment_clear(alignment);
            free(alignment);
            free(dp);
            return status;
        }
    }
    free(dp);
    *out = alignment;
    return CG_OK;
}

static cg_status self_score(
    const char *const *items,
    size_t count,
    cg_score_fn score,
    void *user,
    double *out
) {
    size_t i;
    double sum = 0.0;

    for (i = 0; i < count; i++) {
        double s = 0.0;
        cg_status status = score_cell(items[i], items[i], score, user, &s);
        if (status != CG_OK) {
            return status;
        }
        sum += s;
    }
    *out = sum;
    return CG_OK;
}

static cg_status distance_generic(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    double *out
) {
    cg_alignment *alignment = NULL;
    double aa = 0.0;
    double bb = 0.0;
    double denom;
    double d;
    cg_status status;

    if (out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    if (source_count == 0 && target_count == 0) {
        *out = 0.0;
        return CG_OK;
    }
    if (source_count == 0 || target_count == 0) {
        *out = 1.0;
        return CG_OK;
    }
    status = needleman_wunsch_generic(source, source_count, target, target_count, score, user, &alignment);
    if (status != CG_OK) {
        return status;
    }
    status = self_score(source, source_count, score, user, &aa);
    if (status == CG_OK) {
        status = self_score(target, target_count, score, user, &bb);
    }
    if (status != CG_OK) {
        cg_alignment_free(alignment);
        return status;
    }
    denom = aa + bb;
    if (denom <= 0.0) {
        d = 1.0;
    } else {
        d = 1.0 - (2.0 * alignment->score) / denom;
    }
    if (d < 0.0) {
        d = 0.0;
    }
    if (d > 1.0) {
        d = 1.0;
    }
    cg_alignment_free(alignment);
    *out = d;
    return CG_OK;
}

static cg_status local_score_generic(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    double *out
) {
    size_t m = source_count;
    size_t n = target_count;
    size_t cols = n + 1;
    double *dp;
    double best = 0.0;
    size_t i;
    size_t j;

    if (out == NULL || score == NULL || (source == NULL && source_count > 0) || (target == NULL && target_count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    if (m == 0 || n == 0) {
        *out = 0.0;
        return CG_OK;
    }
    dp = (double *)calloc((m + 1) * (n + 1), sizeof(*dp));
    if (dp == NULL) {
        return CG_ERR_OOM;
    }
    for (i = 1; i <= m; i++) {
        for (j = 1; j <= n; j++) {
            double s;
            double v;
            cg_status status = score_cell(source[i - 1], target[j - 1], score, user, &s);
            if (status != CG_OK) {
                free(dp);
                return status;
            }
            v = max3(
                dp[(i - 1) * cols + (j - 1)] + s,
                dp[(i - 1) * cols + j] - CG_DEFAULT_GAP_COST,
                dp[i * cols + (j - 1)] - CG_DEFAULT_GAP_COST
            );
            if (v < 0.0) {
                v = 0.0;
            }
            dp[i * cols + j] = v;
            if (v > best) {
                best = v;
            }
        }
    }
    free(dp);
    *out = best;
    return CG_OK;
}

static cg_status overlap_score_generic(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    double *out
) {
    size_t m = source_count;
    size_t n = target_count;
    size_t cols = n + 1;
    double *dp;
    double best;
    size_t i;
    size_t j;

    if (out == NULL || score == NULL || (source == NULL && source_count > 0) || (target == NULL && target_count > 0)) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    if (m == 0 || n == 0) {
        *out = 0.0;
        return CG_OK;
    }
    dp = (double *)calloc((m + 1) * (n + 1), sizeof(*dp));
    if (dp == NULL) {
        return CG_ERR_OOM;
    }
    for (i = 1; i <= m; i++) {
        for (j = 1; j <= n; j++) {
            double s;
            cg_status status = score_cell(source[i - 1], target[j - 1], score, user, &s);
            if (status != CG_OK) {
                free(dp);
                return status;
            }
            dp[i * cols + j] = max3(
                dp[(i - 1) * cols + (j - 1)] + s,
                dp[(i - 1) * cols + j] - CG_DEFAULT_GAP_COST,
                dp[i * cols + (j - 1)] - CG_DEFAULT_GAP_COST
            );
        }
    }
    best = dp[m * cols + n];
    for (i = 0; i <= m; i++) {
        if (dp[i * cols + n] > best) {
            best = dp[i * cols + n];
        }
    }
    for (j = 0; j <= n; j++) {
        if (dp[m * cols + j] > best) {
            best = dp[m * cols + j];
        }
    }
    free(dp);
    *out = best;
    return CG_OK;
}

static cg_status combined_distance_generic(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    double *out
) {
    cg_alignment *alignment = NULL;
    double local = 0.0;
    double overlap = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    double denom;
    double d;
    cg_status status;

    if (out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    if (source_count == 0 && target_count == 0) {
        *out = 0.0;
        return CG_OK;
    }
    if (source_count == 0 || target_count == 0) {
        *out = 1.0;
        return CG_OK;
    }
    status = needleman_wunsch_generic(source, source_count, target, target_count, score, user, &alignment);
    if (status == CG_OK) {
        status = local_score_generic(source, source_count, target, target_count, score, user, &local);
    }
    if (status == CG_OK) {
        status = overlap_score_generic(source, source_count, target, target_count, score, user, &overlap);
    }
    if (status == CG_OK) {
        status = self_score(source, source_count, score, user, &aa);
    }
    if (status == CG_OK) {
        status = self_score(target, target_count, score, user, &bb);
    }
    if (status != CG_OK) {
        cg_alignment_free(alignment);
        return status;
    }
    denom = 3.0 * (aa + bb);
    if (denom <= 0.0) {
        d = 1.0;
    } else {
        d = 1.0 - (2.0 * (alignment->score + local + overlap)) / denom;
    }
    if (d < 0.0) {
        d = 0.0;
    }
    if (d > 1.0) {
        d = 1.0;
    }
    cg_alignment_free(alignment);
    *out = d;
    return CG_OK;
}

/* The three alignment views behind the combined distance, each normalized the
 * way its own distance is, plus the two distances the pipeline actually uses.
 * `global` is bit-for-bit what distance_generic returns and `combined` what
 * combined_distance_generic returns: the same calls, in the same order, with
 * the same arithmetic, so a pair export reports the number clustering used.
 * `local` and `overlap` are each 1 - 2s/(aa+bb) clamped to [0, 1]; they are
 * reported for reading, and combined is the clamped mean of the *unclamped*
 * three, which is why it is not always the mean of the reported ones. */
static cg_status components_generic(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    cg_internal_components *out
) {
    cg_alignment *alignment = NULL;
    double local = 0.0;
    double overlap = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    cg_status status;

    if (out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    if (source_count == 0 && target_count == 0) {
        *out = (cg_internal_components){0.0, 0.0, 0.0, 0.0};
        return CG_OK;
    }
    if (source_count == 0 || target_count == 0) {
        *out = (cg_internal_components){1.0, 1.0, 1.0, 1.0};
        return CG_OK;
    }
    status = needleman_wunsch_generic(source, source_count, target, target_count, score, user, &alignment);
    if (status == CG_OK) {
        status = local_score_generic(source, source_count, target, target_count, score, user, &local);
    }
    if (status == CG_OK) {
        status = overlap_score_generic(source, source_count, target, target_count, score, user, &overlap);
    }
    if (status == CG_OK) {
        status = self_score(source, source_count, score, user, &aa);
    }
    if (status == CG_OK) {
        status = self_score(target, target_count, score, user, &bb);
    }
    if (status != CG_OK) {
        cg_alignment_free(alignment);
        return status;
    }
    {
        double denom1 = aa + bb;
        double denom3 = 3.0 * (aa + bb);
        double g;
        double l;
        double o;
        double c;
        if (denom1 <= 0.0) {
            g = 1.0;
            l = 1.0;
            o = 1.0;
        } else {
            g = 1.0 - (2.0 * alignment->score) / denom1;
            l = 1.0 - (2.0 * local) / denom1;
            o = 1.0 - (2.0 * overlap) / denom1;
        }
        if (denom3 <= 0.0) {
            c = 1.0;
        } else {
            c = 1.0 - (2.0 * (alignment->score + local + overlap)) / denom3;
        }
        out->global = g < 0.0 ? 0.0 : (g > 1.0 ? 1.0 : g);
        out->local = l < 0.0 ? 0.0 : (l > 1.0 ? 1.0 : l);
        out->overlap = o < 0.0 ? 0.0 : (o > 1.0 ? 1.0 : o);
        out->combined = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
    }
    cg_alignment_free(alignment);
    return CG_OK;
}

void cg_alignment_free(cg_alignment *alignment) {
    if (alignment == NULL) {
        return;
    }
    alignment_clear(alignment);
    free(alignment);
}

uint64_t cg_alignment_count(const cg_alignment *alignment) {
    return alignment == NULL ? 0 : (uint64_t)alignment->count;
}

const char *cg_alignment_source_at(const cg_alignment *alignment, uint64_t index) {
    if (alignment == NULL || index >= alignment->count) {
        return NULL;
    }
    return alignment->source[index];
}

const char *cg_alignment_target_at(const cg_alignment *alignment, uint64_t index) {
    if (alignment == NULL || index >= alignment->count) {
        return NULL;
    }
    return alignment->target[index];
}

double cg_alignment_score(const cg_alignment *alignment) {
    return alignment == NULL ? 0.0 : alignment->score;
}

cg_status cg_align_needleman_wunsch(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    cg_alignment **out
) {
    cg_prior_score_user user = {ctx, prosody_aware};

    if (ctx == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    return needleman_wunsch_generic(source, (size_t)source_count, target, (size_t)target_count, score_prior_cb, &user, out);
}

cg_status cg_align_distance(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    double *out
) {
    cg_prior_score_user user = {ctx, prosody_aware};

    if (ctx == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    return distance_generic(source, (size_t)source_count, target, (size_t)target_count, score_prior_cb, &user, out);
}

cg_status cg_align_local_score(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    double *out
) {
    cg_prior_score_user user = {ctx, prosody_aware};

    if (ctx == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    return local_score_generic(source, (size_t)source_count, target, (size_t)target_count, score_prior_cb, &user, out);
}

cg_status cg_align_overlap_score(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    double *out
) {
    cg_prior_score_user user = {ctx, prosody_aware};

    if (ctx == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    return overlap_score_generic(source, (size_t)source_count, target, (size_t)target_count, score_prior_cb, &user, out);
}

cg_status cg_align_distance_combined(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    double *out
) {
    cg_prior_score_user user = {ctx, prosody_aware};

    if (ctx == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    return combined_distance_generic(source, (size_t)source_count, target, (size_t)target_count, score_prior_cb, &user, out);
}

cg_status cg_internal_needleman_wunsch(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    cg_alignment **out
) {
    return needleman_wunsch_generic(source, source_count, target, target_count, score, user, out);
}

cg_status cg_internal_distance(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    double *out
) {
    return distance_generic(source, source_count, target, target_count, score, user, out);
}

cg_status cg_internal_distance_combined(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    double *out
) {
    return combined_distance_generic(source, source_count, target, target_count, score, user, out);
}

cg_status cg_internal_distance_components(
    const char *const *source,
    size_t source_count,
    const char *const *target,
    size_t target_count,
    cg_score_fn score,
    void *user,
    cg_internal_components *out
) {
    return components_generic(source, source_count, target, target_count, score, user, out);
}
