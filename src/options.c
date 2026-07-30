#include "cognator.h"

#include <string.h>

const char *cg_version_string(void) {
    return CG_VERSION_STRING;
}

int cg_version_major(void) {
    return CG_VERSION_MAJOR;
}

int cg_version_minor(void) {
    return CG_VERSION_MINOR;
}

int cg_version_patch(void) {
    return CG_VERSION_PATCH;
}

uint32_t cg_abi_version(void) {
    return CG_ABI_VERSION;
}

const char *cg_status_string(cg_status status) {
    static const char *const messages[] = {
        "success",
        "invalid argument",
        "I/O error",
        "parse error",
        "merkmal error",
        "unsupported option",
        "out of memory"
    };
    if (status < CG_OK || status > CG_ERR_OOM) {
        return "unknown error";
    }
    return messages[status];
}

void cg_options_init_defaults(cg_options *options) {
    if (options == NULL) {
        return;
    }
    *options = (cg_options) {
        .iterations = 3,
        .lambda = 0.5,
        .threshold = 0.0,
        .threshold_mode = "family",
        .threshold_min = 0.2,
        .threshold_max = 0.8,
        .threshold_shrink = 0.5,
        .graph_mode = "threshold",
        .graph_expansion_mode = "either",
        .graph_top_k = 2,
        .graph_adaptive_margin = 0.1,
        .graph_adaptive_either_max = 0.15,
        .graph_adaptive_ambiguity = 0.5,
        .graph_mutual_boost = 0.2,
        .graph_triangle_boost = 0.3,
        .graph_contrastive_penalty = 0.0,
        .graph_contrastive_floor = 0.05,
        .graph_risk_rank_weight = 0.5,
        .graph_risk_gap_weight = 0.5,
        .graph_unilateral_penalty = 0.0,
        .graph_rival_floor = 0.6,
        .refine_mode = "none",
        .refine_confidence_threshold = 0.55,
        .refine_bridge_ratio = 0.7,
        .seed = UINT64_C(1),
        .jobs = 0,
        .bootstrap = 0,
        .bootstrap_sigma = 0.05,
        .cluster_scope = "concept",
        .min_pairs = 5,
        .alignment_mode = "combined",
        .class_reduce = 1,
        .null_mode = "marginal",
        .null_max_runs = 200,
        .scorer_mode = "posterior",
        .dirichlet_concentration = 50.0,
        .dirichlet_sharpness = 4.0,
        .dirichlet_scale_mode = "",
        .merkmal_system = "descriptive"
    };
}

cg_status cg_options_apply_preset(cg_options *options, const char *preset) {
    if (options == NULL || preset == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    if (strcmp(preset, "default") == 0) {
        cg_options_init_defaults(options);
        return CG_OK;
    }
    if (strcmp(preset, "support-adaptive-bridge") == 0) {
        cg_options_init_defaults(options);
        options->threshold = 0.68;
        options->graph_mode = "support";
        options->graph_expansion_mode = "adaptive";
        options->graph_top_k = 3;
        options->graph_contrastive_penalty = 0.20;
        options->refine_mode = "bridge-cut";
        return CG_OK;
    }
    return CG_ERR_UNSUPPORTED_OPTION;
}
