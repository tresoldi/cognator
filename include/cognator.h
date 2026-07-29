#ifndef COGNATOR_H
#define COGNATOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(COGNATOR_SHARED)
#  if defined(COGNATOR_BUILDING_LIBRARY)
#    define CG_API __declspec(dllexport)
#  else
#    define CG_API __declspec(dllimport)
#  endif
#elif defined(COGNATOR_SHARED) && defined(__GNUC__)
#  define CG_API __attribute__((visibility("default")))
#else
#  define CG_API
#endif

#define CG_VERSION_MAJOR 0
#define CG_VERSION_MINOR 1
#define CG_VERSION_PATCH 0
#define CG_VERSION_STRING "0.1.0"

typedef struct cg_context cg_context;
typedef struct cg_records cg_records;
typedef struct cg_result cg_result;

typedef enum cg_status {
    CG_OK = 0,
    CG_ERR_INVALID_ARGUMENT,
    CG_ERR_IO,
    CG_ERR_PARSE,
    CG_ERR_MERKMAL,
    CG_ERR_UNSUPPORTED_OPTION,
    CG_ERR_OOM
} cg_status;

typedef struct cg_options {
    int iterations;
    double lambda;
    double threshold;
    const char *threshold_mode;
    double threshold_min;
    double threshold_max;
    double threshold_shrink;
    const char *graph_mode;
    const char *graph_expansion_mode;
    int graph_top_k;
    double graph_adaptive_margin;
    double graph_adaptive_either_max;
    double graph_adaptive_ambiguity;
    double graph_mutual_boost;
    double graph_triangle_boost;
    double graph_contrastive_penalty;
    double graph_contrastive_floor;
    double graph_risk_rank_weight;
    double graph_risk_gap_weight;
    double graph_unilateral_penalty;
    double graph_rival_floor;
    const char *refine_mode;
    double refine_confidence_threshold;
    double refine_bridge_ratio;
    uint64_t seed;
    int jobs;
    int bootstrap;
    double bootstrap_sigma;
    const char *cluster_scope;
    int min_pairs;
    const char *alignment_mode;
    int class_reduce;
    const char *null_mode;
    int null_max_runs;
    const char *scorer_mode;
    double dirichlet_concentration;
    double dirichlet_sharpness;
    const char *dirichlet_scale_mode;
    const char *merkmal_system;
} cg_options;

CG_API const char *cg_version_string(void);
CG_API const char *cg_status_string(cg_status status);
CG_API void cg_options_init_defaults(cg_options *options);
CG_API cg_status cg_options_apply_preset(cg_options *options, const char *preset);

#ifdef __cplusplus
}
#endif

#endif
