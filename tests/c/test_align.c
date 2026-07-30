#include "cognator.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static int has_pair(const cg_alignment *al, const char *source, const char *target) {
    uint64_t i;

    for (i = 0; i < cg_alignment_count(al); i++) {
        if (strcmp(cg_alignment_source_at(al, i), source) == 0 &&
            strcmp(cg_alignment_target_at(al, i), target) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    cg_context *ctx = NULL;
    cg_alignment *al = NULL;
    double d = 0.0;
    double d2 = 0.0;
    double plain = 0.0;
    double prosody = 0.0;
    double local = 0.0;
    double overlap = 0.0;
    const char *pake[] = {"p", "a", "k", "e"};
    const char *pa[] = {"p", "a"};
    const char *pat[] = {"p", "a", "t"};
    const char *nokte[] = {"n", "o", "k", "t", "e"};
    const char *notte[] = {"n", "o", "t", "t", "e"};
    const char *noche[] = {"n", "o", "tʃ", "e"};
    const char *muro[] = {"m", "u", "r", "o"};

    assert(cg_context_new_builtin(&ctx) == CG_OK);

    assert(cg_align_needleman_wunsch(ctx, pake, 4, pake, 4, 0, &al) == CG_OK);
    assert(cg_alignment_count(al) == 4);
    assert(cg_align_distance(ctx, pake, 4, pake, 4, 0, &d) == CG_OK);
    assert(d == 0.0);
    cg_alignment_free(al);

    assert(cg_align_needleman_wunsch(ctx, pa, 2, pat, 3, 0, &al) == CG_OK);
    assert(cg_alignment_count(al) == 3);
    assert(has_pair(al, "", "t"));
    cg_alignment_free(al);

    assert(cg_align_distance(ctx, pake, 4, muro, 4, 0, &d) == CG_OK);
    assert(d >= 0.0 && d <= 1.0);

    assert(cg_align_needleman_wunsch(ctx, nokte, 5, notte, 5, 0, &al) == CG_OK);
    assert(has_pair(al, "k", "t"));
    assert(has_pair(al, "t", "t"));
    cg_alignment_free(al);
    assert(cg_align_distance(ctx, nokte, 5, notte, 5, 0, &d) == CG_OK);
    assert(d > 0.0 && d < 0.3);
    assert(cg_align_distance(ctx, nokte, 5, noche, 4, 0, &d2) == CG_OK);
    assert(d2 > d);

    assert(cg_align_distance(ctx, pake, 4, pat, 3, 0, &d) == CG_OK);
    assert(cg_align_distance(ctx, pat, 3, pake, 4, 0, &d2) == CG_OK);
    assert(fabs(d - d2) < 1e-9);

    assert(cg_prior_score(ctx, "p", "a", 0, &plain) == CG_OK);
    assert(cg_prior_score(ctx, "p", "a", 1, &prosody) == CG_OK);
    assert(fabs((plain - prosody) - 0.3) < 1e-9);
    assert(cg_prior_score(ctx, "p", "t", 0, &plain) == CG_OK);
    assert(cg_prior_score(ctx, "p", "t", 1, &prosody) == CG_OK);
    assert(plain == prosody);

    assert(cg_align_local_score(ctx, nokte, 5, noche, 4, 1, &local) == CG_OK);
    assert(local > 0.0);
    assert(cg_align_overlap_score(ctx, nokte, 5, noche, 4, 1, &overlap) == CG_OK);
    assert(overlap > 0.0);
    assert(cg_align_distance_combined(ctx, nokte, 5, noche, 4, 1, &d) == CG_OK);
    assert(d >= 0.0 && d <= 1.0);

    assert(cg_align_needleman_wunsch(NULL, pa, 2, pat, 3, 0, &al) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_align_needleman_wunsch(ctx, NULL, 2, pat, 3, 0, &al) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_align_needleman_wunsch(ctx, pa, 2, pat, 3, 0, NULL) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_alignment_count(NULL) == 0);
    assert(cg_alignment_source_at(NULL, 0) == NULL);
    cg_alignment_free(NULL);
    cg_context_free(ctx);
    return 0;
}
