#include "internal/context.h"

#include <stdlib.h>
#include <string.h>

static int feature_set_contains(const mk_feature_set *features, const char *value) {
    size_t i;

    for (i = 0; i < mk_feature_set_size(features); i++) {
        const char *item = mk_feature_set_get(features, i);
        if (item != NULL && strcmp(item, value) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_tone_token(const char *grapheme) {
    const unsigned char *p = (const unsigned char *)grapheme;

    if (grapheme == NULL || grapheme[0] == '\0') {
        return 0;
    }
    while (*p != '\0') {
        if (*p >= '0' && *p <= '9') {
            p++;
            continue;
        }
        if ((p[0] == 0xCB && p[1] >= 0xA5 && p[1] <= 0xA9) ||
            (p[0] == 0xC2 && (p[1] == 0xB9 || p[1] == 0xB2 || p[1] == 0xB3)) ||
            (p[0] == 0xE2 && p[1] == 0x81 && (p[2] == 0xB0 || p[2] == 0xB4 || p[2] == 0xB5))) {
            p += (p[0] == 0xE2) ? 3 : 2;
            continue;
        }
        return 0;
    }
    return 1;
}

static int is_standalone_suprasegmental(const char *grapheme) {
    return strcmp(grapheme, "ˈ") == 0 ||
        strcmp(grapheme, "ˌ") == 0 ||
        strcmp(grapheme, "ː") == 0 ||
        strcmp(grapheme, "ˑ") == 0;
}

static cg_role role_from_features(const mk_feature_set *features) {
    if (feature_set_contains(features, "vowel") || feature_set_contains(features, "syllabic=+")) {
        return CG_ROLE_V;
    }
    if (feature_set_contains(features, "consonant") ||
        feature_set_contains(features, "consonantal=+") ||
        feature_set_contains(features, "consonant=+")) {
        if (feature_set_contains(features, "nasal") ||
            feature_set_contains(features, "lateral") ||
            feature_set_contains(features, "trill") ||
            feature_set_contains(features, "tap") ||
            feature_set_contains(features, "flap") ||
            feature_set_contains(features, "sonorant") ||
            feature_set_contains(features, "nasal=+") ||
            feature_set_contains(features, "lateral=+") ||
            feature_set_contains(features, "trill=+") ||
            feature_set_contains(features, "tap=+") ||
            feature_set_contains(features, "sonorant=+")) {
            return CG_ROLE_R;
        }
        if (feature_set_contains(features, "approximant") ||
            feature_set_contains(features, "semi-vowel") ||
            feature_set_contains(features, "approximant=+")) {
            return CG_ROLE_G;
        }
        if (feature_set_contains(features, "stop") ||
            feature_set_contains(features, "plosive") ||
            feature_set_contains(features, "fricative") ||
            feature_set_contains(features, "affricate") ||
            feature_set_contains(features, "implosive") ||
            feature_set_contains(features, "click")) {
            return CG_ROLE_C;
        }
        return CG_ROLE_C;
    }
    if (feature_set_contains(features, "sonorant=+") && feature_set_contains(features, "approximant=+")) {
        return CG_ROLE_G;
    }
    return CG_ROLE_UNKNOWN;
}

/* Compiled in by the build, which reads them from third_party/merkmal/PINNED
 * or from an external checkout. An #error rather than a default: regulae
 * shipped an empty provenance string for months because the build quietly
 * substituted one, and a wrong answer here is worse than no build. */
#ifndef COGNATOR_MERKMAL_VERSION
#error "COGNATOR_MERKMAL_VERSION must be defined by the build"
#endif
#ifndef COGNATOR_MERKMAL_COMMIT
#error "COGNATOR_MERKMAL_COMMIT must be defined by the build"
#endif

const char *cg_merkmal_version(void) {
    return COGNATOR_MERKMAL_VERSION;
}

const char *cg_merkmal_commit(void) {
    return COGNATOR_MERKMAL_COMMIT;
}

cg_status cg_from_mk_status(mk_status status) {
    switch (status) {
    case MK_OK:
        return CG_OK;
    case MK_ERR_INVALID_ARGUMENT:
        return CG_ERR_INVALID_ARGUMENT;
    case MK_ERR_UNKNOWN_SYSTEM:
    case MK_ERR_UNKNOWN_GRAPHEME:
        return CG_ERR_MERKMAL;
    case MK_ERR_UNSUPPORTED_MODEL:
        return CG_ERR_UNSUPPORTED_OPTION;
    case MK_ERR_PARSE:
        return CG_ERR_PARSE;
    case MK_ERR_OOM:
        return CG_ERR_OOM;
    default:
        return CG_ERR_MERKMAL;
    }
}

cg_status cg_context_new_builtin(cg_context **out) {
    cg_context *ctx;
    mk_status status;

    if (out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    ctx = (cg_context *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return CG_ERR_OOM;
    }
    status = mk_registry_new_builtin(&ctx->registry);
    if (status != MK_OK) {
        free(ctx);
        return cg_from_mk_status(status);
    }
    status = mk_registry_get_system(ctx->registry, "descriptive", &ctx->system);
    if (status != MK_OK) {
        mk_registry_free(ctx->registry);
        free(ctx);
        return cg_from_mk_status(status);
    }
    *out = ctx;
    return CG_OK;
}

void cg_context_free(cg_context *ctx) {
    if (ctx == NULL) {
        return;
    }
    mk_registry_free(ctx->registry);
    free(ctx);
}

cg_status cg_context_use_system(cg_context *ctx, const char *system_name) {
    mk_status status;
    const mk_system *system = NULL;

    if (ctx == NULL || system_name == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    status = mk_registry_get_system(ctx->registry, system_name, &system);
    if (status != MK_OK) {
        return cg_from_mk_status(status);
    }
    ctx->system = system;
    return CG_OK;
}

cg_status cg_context_system_name(const cg_context *ctx, const char **out) {
    mk_status status;

    if (ctx == NULL || ctx->system == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    status = mk_system_name(ctx->system, out);
    return cg_from_mk_status(status);
}

cg_status cg_context_is_segment(const cg_context *ctx, const char *grapheme, int *out) {
    mk_status status;

    if (ctx == NULL || ctx->system == NULL || grapheme == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = 0;
    status = mk_system_is_segment(ctx->system, grapheme, out);
    return cg_from_mk_status(status);
}

cg_status cg_context_segment_distance(
    const cg_context *ctx,
    const char *a,
    const char *b,
    double *out
) {
    mk_status status;

    if (ctx == NULL || ctx->system == NULL || a == NULL || b == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = 0.0;
    if (strcmp(a, b) == 0) {
        return CG_OK;
    }
    status = mk_system_segment_distance(ctx->system, a, b, out);
    return cg_from_mk_status(status);
}

cg_status cg_context_grapheme_role(
    const cg_context *ctx,
    const char *grapheme,
    cg_role *out
) {
    mk_feature_set *features = NULL;
    mk_status status;

    if (ctx == NULL || ctx->system == NULL || grapheme == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = CG_ROLE_UNKNOWN;
    status = mk_system_grapheme_features(ctx->system, grapheme, &features);
    if (status == MK_OK) {
        *out = role_from_features(features);
        mk_feature_set_free(features);
        return CG_OK;
    }
    if (status == MK_ERR_UNKNOWN_GRAPHEME) {
        if (is_tone_token(grapheme)) {
            *out = CG_ROLE_T;
            return CG_OK;
        }
        if (is_standalone_suprasegmental(grapheme)) {
            *out = CG_ROLE_S;
            return CG_OK;
        }
    }
    return cg_from_mk_status(status);
}
