#include "cognator.h"

#include <assert.h>
#include <math.h>
#include <string.h>

int main(void) {
    cg_context *ctx = NULL;
    const char *name = NULL;
    int is_segment = 0;
    double distance = 0.0;
    cg_role role = CG_ROLE_UNKNOWN;

    assert(cg_context_new_builtin(&ctx) == CG_OK);
    assert(ctx != NULL);

    assert(cg_context_system_name(ctx, &name) == CG_OK);
    assert(strcmp(name, "descriptive") == 0);

    assert(cg_context_is_segment(ctx, "p", &is_segment) == CG_OK);
    assert(is_segment == 1);
    assert(cg_context_is_segment(ctx, "not-ipa", &is_segment) == CG_OK);
    assert(is_segment == 0);

    assert(cg_context_segment_distance(ctx, "p", "p", &distance) == CG_OK);
    assert(distance == 0.0);
    assert(cg_context_segment_distance(ctx, "p", "b", &distance) == CG_OK);
    assert(distance > 0.0 && distance < 1.0);

    assert(cg_context_grapheme_role(ctx, "p", &role) == CG_OK);
    assert(role == CG_ROLE_C);
    assert(cg_context_grapheme_role(ctx, "a", &role) == CG_OK);
    assert(role == CG_ROLE_V);
    assert(cg_context_grapheme_role(ctx, "m", &role) == CG_OK);
    assert(role == CG_ROLE_R);
    assert(cg_context_grapheme_role(ctx, "j", &role) == CG_OK);
    assert(role == CG_ROLE_G);
    assert(cg_context_grapheme_role(ctx, "³¹", &role) == CG_OK);
    assert(role == CG_ROLE_T);
    assert(cg_context_grapheme_role(ctx, "ˈ", &role) == CG_OK);
    assert(role == CG_ROLE_S);

    assert(cg_context_use_system(ctx, "phoible") == CG_OK);
    assert(cg_context_system_name(ctx, &name) == CG_OK);
    assert(strcmp(name, "phoible") == 0);
    assert(cg_context_use_system(ctx, "missing") == CG_ERR_MERKMAL);

    assert(cg_context_new_builtin(NULL) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_context_system_name(NULL, &name) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_context_is_segment(ctx, NULL, &is_segment) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_context_segment_distance(ctx, "p", NULL, &distance) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_context_grapheme_role(ctx, NULL, &role) == CG_ERR_INVALID_ARGUMENT);

    cg_context_free(ctx);
    cg_context_free(NULL);
    return 0;
}
