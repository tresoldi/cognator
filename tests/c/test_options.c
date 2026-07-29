#include "cognator.h"

#include <assert.h>
#include <string.h>

int main(void) {
    cg_options options;
    cg_options_init_defaults(&options);
    assert(options.iterations == 3);
    assert(options.class_reduce == 1);
    assert(strcmp(options.cluster_scope, "concept") == 0);
    assert(strcmp(options.alignment_mode, "combined") == 0);
    assert(strcmp(options.scorer_mode, "posterior") == 0);
    assert(strcmp(options.merkmal_system, "descriptive") == 0);
    assert(cg_options_apply_preset(&options, "support-adaptive-bridge") == CG_OK);
    assert(options.threshold == 0.68);
    assert(options.graph_top_k == 3);
    assert(options.graph_contrastive_penalty == 0.20);
    assert(strcmp(options.refine_mode, "bridge-cut") == 0);
    assert(cg_options_apply_preset(&options, "missing") == CG_ERR_UNSUPPORTED_OPTION);
    assert(cg_options_apply_preset(NULL, "default") == CG_ERR_INVALID_ARGUMENT);
    assert(strcmp(cg_status_string(CG_ERR_PARSE), "parse error") == 0);
    assert(strcmp(cg_status_string((cg_status) 999), "unknown error") == 0);
    assert(strcmp(cg_version_string(), CG_VERSION_STRING) == 0);
    return 0;
}
