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
#define CG_ABI_VERSION 1

typedef struct cg_context cg_context;
typedef struct cg_records cg_records;
typedef struct cg_result cg_result;
typedef struct cg_tokens cg_tokens;
typedef struct cg_oov_report cg_oov_report;
typedef struct cg_alignment cg_alignment;
typedef struct cg_dirichlet_prior cg_dirichlet_prior;
typedef struct cg_posterior_table cg_posterior_table;
typedef struct cg_graph cg_graph;
typedef struct cg_partition cg_partition;
typedef struct cg_diagnostics cg_diagnostics;
typedef struct cg_bootstrap_summary cg_bootstrap_summary;

typedef enum cg_role {
    CG_ROLE_C = 'C',
    CG_ROLE_V = 'V',
    CG_ROLE_R = 'R',
    CG_ROLE_G = 'G',
    CG_ROLE_T = 'T',
    CG_ROLE_S = 'S',
    CG_ROLE_UNKNOWN = 'X'
} cg_role;

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

typedef struct cg_record {
    const char *id;
    const char *language_id;
    const char *concept;
    const char *segments;
    const char *family;
    const char *cognacy;
} cg_record;

typedef struct cg_sidecar_row {
    const char *id;
    const char *cognator_id;
    double confidence;
    double assigned_support;
    double alternative_support;
    double support_margin;
    int cluster_size;
    int candidate_neighbors;
    double bootstrap;
    double bootstrap_low;
    double bootstrap_high;
} cg_sidecar_row;

typedef struct cg_token {
    const char *grapheme;
    const char *tone;
    int boundary;
    int oov;
    cg_role role;
} cg_token;

typedef struct cg_oov_row {
    const char *grapheme;
    uint64_t count;
} cg_oov_row;

typedef struct cg_form_pair {
    const char *const *source;
    uint64_t source_count;
    const char *const *target;
    uint64_t target_count;
    const char *concept;
    const char *source_id;
    const char *target_id;
} cg_form_pair;

typedef struct cg_distance_pair {
    uint64_t a;
    uint64_t b;
    double distance;
} cg_distance_pair;

typedef struct cg_edge {
    uint64_t a;
    uint64_t b;
    double weight;
} cg_edge;

typedef struct cg_graph_spec {
    const char *mode;
    const char *expansion_mode;
    int top_k;
    double adaptive_margin;
    double adaptive_either_max;
    double adaptive_ambiguity;
    double mutual_boost;
    double triangle_boost;
    double contrastive_penalty;
    double contrastive_floor;
    double risk_rank_weight;
    double risk_gap_weight;
    double unilateral_penalty;
    double rival_floor;
} cg_graph_spec;

typedef struct cg_refine_spec {
    const char *mode;
    double confidence_threshold;
    double bridge_ratio;
} cg_refine_spec;

typedef struct cg_diagnostic {
    double confidence;
    double assigned_support;
    double alternative_support;
    double support_margin;
    uint64_t cluster_size;
    uint64_t candidate_neighbors;
} cg_diagnostic;

typedef struct cg_eval_result {
    uint64_t scored_count;
    uint64_t pred_count;
    uint64_t truth_count;
    double precision;
    double recall;
    double f1;
} cg_eval_result;

CG_API const char *cg_version_string(void);
CG_API int cg_version_major(void);
CG_API int cg_version_minor(void);
CG_API int cg_version_patch(void);
CG_API uint32_t cg_abi_version(void);
CG_API const char *cg_status_string(cg_status status);
/* Release strings returned through owned char** outputs, such as cg_class_reduce. */
CG_API void cg_string_free(char *value);
CG_API void cg_options_init_defaults(cg_options *options);
CG_API cg_status cg_options_apply_preset(cg_options *options, const char *preset);

CG_API cg_status cg_context_new_builtin(cg_context **out);
CG_API void cg_context_free(cg_context *ctx);
CG_API cg_status cg_context_use_system(cg_context *ctx, const char *system_name);
CG_API cg_status cg_context_system_name(const cg_context *ctx, const char **out);
CG_API cg_status cg_context_is_segment(const cg_context *ctx, const char *grapheme, int *out);
CG_API cg_status cg_context_segment_distance(
    const cg_context *ctx,
    const char *a,
    const char *b,
    double *out
);
CG_API cg_status cg_context_grapheme_role(
    const cg_context *ctx,
    const char *grapheme,
    cg_role *out
);

CG_API cg_status cg_read_table(const char *path, cg_records **out);
CG_API void cg_records_free(cg_records *records);
CG_API uint64_t cg_records_count(const cg_records *records);
/* Borrowed; valid until cg_records_free(records). */
CG_API const cg_record *cg_records_get(const cg_records *records, uint64_t index);
/* Returns a new owned record set; release with cg_records_free. */
CG_API cg_status cg_records_filter_family(
    const cg_records *records,
    const char *family,
    cg_records **out
);

CG_API cg_status cg_write_sidecar_rows(
    const char *path,
    const cg_sidecar_row *rows,
    uint64_t count
);
CG_API cg_status cg_detect(
    cg_context *ctx,
    const cg_records *records,
    const cg_options *options,
    cg_result **out
);
CG_API void cg_result_free(cg_result *result);
CG_API uint64_t cg_result_row_count(const cg_result *result);
/* Borrowed; valid until cg_result_free(result). */
CG_API const cg_sidecar_row *cg_result_row_at(const cg_result *result, uint64_t index);
/* Deep-copy borrowed row strings into out; release with cg_sidecar_row_clear. */
CG_API cg_status cg_sidecar_row_copy(const cg_sidecar_row *row, cg_sidecar_row *out);
CG_API void cg_sidecar_row_clear(cg_sidecar_row *row);
CG_API double cg_result_threshold(const cg_result *result);
CG_API uint64_t cg_result_oov_count(const cg_result *result);
/* Borrowed; valid until cg_result_free(result). */
CG_API const cg_oov_row *cg_result_oov_at(const cg_result *result, uint64_t index);
/* Deep-copy borrowed row strings into out; release with cg_oov_row_clear. */
CG_API cg_status cg_oov_row_copy(const cg_oov_row *row, cg_oov_row *out);
CG_API void cg_oov_row_clear(cg_oov_row *row);
CG_API cg_status cg_write_sidecar(const char *path, const cg_result *result);
CG_API cg_status cg_write_oov_report(const char *path, const cg_result *result);
CG_API cg_status cg_eval_b_cubed(
    const char *pred_path,
    const char *truth_path,
    const char *truth_col,
    int per_concept,
    cg_eval_result *out
);
CG_API cg_status cg_eval_b_cubed_result(
    const cg_result *pred,
    const cg_records *truth,
    const char *truth_col,
    int per_concept,
    cg_eval_result *out
);

CG_API cg_status cg_segment_parse(
    const cg_context *ctx,
    const char *raw,
    cg_tokens **out
);
CG_API cg_status cg_segment_parse_with_oov(
    const cg_context *ctx,
    const char *raw,
    cg_oov_report *oov_report,
    cg_tokens **out
);
CG_API cg_status cg_class_reduce(const char *grapheme, char **out);
CG_API void cg_tokens_free(cg_tokens *tokens);
CG_API uint64_t cg_tokens_count(const cg_tokens *tokens);
/* Borrowed; valid until cg_tokens_free(tokens). */
CG_API const cg_token *cg_tokens_get(const cg_tokens *tokens, uint64_t index);

CG_API cg_status cg_oov_report_new(cg_oov_report **out);
CG_API void cg_oov_report_free(cg_oov_report *report);
CG_API cg_status cg_oov_report_add(cg_oov_report *report, const char *grapheme);
CG_API uint64_t cg_oov_report_count(const cg_oov_report *report);
/* Borrowed; valid until cg_oov_report_free(report). */
CG_API const cg_oov_row *cg_oov_report_get(const cg_oov_report *report, uint64_t index);
CG_API cg_status cg_write_oov_report_rows(
    const char *path,
    const cg_oov_row *rows,
    uint64_t count
);

CG_API void cg_alignment_free(cg_alignment *alignment);
CG_API uint64_t cg_alignment_count(const cg_alignment *alignment);
CG_API const char *cg_alignment_source_at(const cg_alignment *alignment, uint64_t index);
CG_API const char *cg_alignment_target_at(const cg_alignment *alignment, uint64_t index);
CG_API double cg_alignment_score(const cg_alignment *alignment);
CG_API cg_status cg_align_needleman_wunsch(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    cg_alignment **out
);
CG_API cg_status cg_align_distance(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    double *out
);
CG_API cg_status cg_align_local_score(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    double *out
);
CG_API cg_status cg_align_overlap_score(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    double *out
);
CG_API cg_status cg_align_distance_combined(
    const cg_context *ctx,
    const char *const *source,
    uint64_t source_count,
    const char *const *target,
    uint64_t target_count,
    int prosody_aware,
    double *out
);
CG_API cg_status cg_prior_score(
    const cg_context *ctx,
    const char *source,
    const char *target,
    int prosody_aware,
    double *out
);

CG_API cg_status cg_dirichlet_prior_new(
    const cg_context *ctx,
    double concentration,
    double sharpness,
    const char *const *inventory,
    uint64_t inventory_count,
    cg_dirichlet_prior **out
);
CG_API void cg_dirichlet_prior_free(cg_dirichlet_prior *prior);
CG_API uint64_t cg_dirichlet_prior_inventory_count(const cg_dirichlet_prior *prior);
/* Borrowed; valid until cg_dirichlet_prior_free(prior). */
CG_API const char *cg_dirichlet_prior_inventory_at(const cg_dirichlet_prior *prior, uint64_t index);
CG_API cg_status cg_dirichlet_prior_alpha(
    const cg_dirichlet_prior *prior,
    const char *source,
    const char *target,
    double *out
);
CG_API cg_status cg_inventory_from_pairs(
    const cg_form_pair *pairs,
    uint64_t pair_count,
    char ***inventory_out,
    uint64_t *inventory_count_out
);
CG_API void cg_string_array_free(char **items, uint64_t count);

CG_API cg_status cg_posterior_table_new(
    const char *source_lect,
    const char *target_lect,
    const cg_dirichlet_prior *prior,
    cg_posterior_table **out
);
CG_API void cg_posterior_table_free(cg_posterior_table *table);
CG_API cg_status cg_posterior_table_add(
    cg_posterior_table *table,
    const char *source,
    const char *target
);
CG_API cg_status cg_posterior_table_attach_scaling(
    cg_posterior_table *table,
    const char *mode
);
CG_API uint64_t cg_posterior_table_count_pair(
    const cg_posterior_table *table,
    const char *source,
    const char *target
);
CG_API uint64_t cg_posterior_table_columns(const cg_posterior_table *table);
CG_API uint64_t cg_posterior_table_total_pairs(const cg_posterior_table *table);
CG_API cg_status cg_posterior_mean(
    const cg_posterior_table *table,
    const char *source,
    const char *target,
    double *out
);
CG_API cg_status cg_posterior_log_odds(
    const cg_posterior_table *table,
    const char *source,
    const char *target,
    double *out
);
CG_API cg_status cg_posterior_score(
    const cg_context *ctx,
    const cg_posterior_table *table,
    const char *source,
    const char *target,
    int prosody_aware,
    double *out
);
CG_API cg_status cg_train_posterior(
    const cg_context *ctx,
    const char *source_lect,
    const char *target_lect,
    const cg_form_pair *pairs,
    uint64_t pair_count,
    const cg_dirichlet_prior *prior,
    int iterations,
    cg_posterior_table **out
);

CG_API void cg_graph_spec_init_defaults(cg_graph_spec *spec);
CG_API void cg_refine_spec_init_defaults(cg_refine_spec *spec);
CG_API cg_status cg_build_graph_edges(
    uint64_t node_count,
    const cg_distance_pair *pairs,
    uint64_t pair_count,
    double threshold,
    const cg_graph_spec *spec,
    cg_edge **edges_out,
    uint64_t *edge_count_out
);
CG_API void cg_edges_free(cg_edge *edges);
CG_API cg_status cg_graph_new(uint64_t node_count, const cg_edge *edges, uint64_t edge_count, cg_graph **out);
CG_API void cg_graph_free(cg_graph *graph);
CG_API uint64_t cg_graph_node_count(const cg_graph *graph);
CG_API uint64_t cg_graph_neighbor_count(const cg_graph *graph, uint64_t node);
CG_API double cg_graph_map_equation(const cg_graph *graph, const uint64_t *modules, uint64_t module_count);
CG_API cg_status cg_cluster_infomap(const cg_graph *graph, uint64_t seed, cg_partition **out);
CG_API cg_status cg_partition_from_modules(const uint64_t *modules, uint64_t count, cg_partition **out);
CG_API void cg_partition_free(cg_partition *partition);
CG_API uint64_t cg_partition_count(const cg_partition *partition);
CG_API uint64_t cg_partition_module_at(const cg_partition *partition, uint64_t node);
CG_API cg_status cg_refine_partition(
    const cg_graph *graph,
    const cg_partition *partition,
    const cg_refine_spec *spec,
    cg_partition **out
);
CG_API cg_status cg_diagnostics_new(
    const cg_graph *graph,
    const cg_partition *partition,
    cg_diagnostics **out
);
CG_API void cg_diagnostics_free(cg_diagnostics *diagnostics);
CG_API uint64_t cg_diagnostics_count(const cg_diagnostics *diagnostics);
/* Borrowed; valid until cg_diagnostics_free(diagnostics). */
CG_API const cg_diagnostic *cg_diagnostics_get(const cg_diagnostics *diagnostics, uint64_t node);
CG_API double cg_confidence_from_support(double in_mean, double alt_weight);
CG_API cg_status cg_bootstrap_confidences(
    uint64_t node_count,
    const cg_distance_pair *pairs,
    uint64_t pair_count,
    const cg_partition *canonical,
    double threshold,
    int runs,
    double sigma,
    uint64_t seed,
    const cg_graph_spec *graph_spec,
    cg_bootstrap_summary **out
);
CG_API void cg_bootstrap_summary_free(cg_bootstrap_summary *summary);
CG_API uint64_t cg_bootstrap_summary_count(const cg_bootstrap_summary *summary);
CG_API double cg_bootstrap_mean_at(const cg_bootstrap_summary *summary, uint64_t node);
CG_API double cg_bootstrap_low_at(const cg_bootstrap_summary *summary, uint64_t node);
CG_API double cg_bootstrap_high_at(const cg_bootstrap_summary *summary, uint64_t node);
CG_API double cg_otsu_threshold(const double *values, uint64_t count, double lo, double hi, int bins);
CG_API double cg_clamp(double value, double min, double max);

#ifdef __cplusplus
}
#endif

#endif
