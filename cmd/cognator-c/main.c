#include "cognator.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct detect_args {
    const char *in;
    const char *out;
    const char *oov_out;
    int max_forms;
    int max_lects;
    cg_options options;
} detect_args;

typedef struct sweep_args {
    const char *in;
    const char *out_tsv;
    const char *thresholds;
    cg_options options;
} sweep_args;

typedef struct bench_args {
    const char *in;
    const char *out_tsv;
    const char *families;
    int max_forms;
    int max_lects;
    int min_cognacy;
    cg_options options;
} bench_args;

static void usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator <command> [options]\n\n"
            "commands:\n"
            "  detect    cluster forms into cognate sets\n"
            "  eval      score predicted clusters against Cognacy truth\n"
            "  sweep     run detect/eval across thresholds\n"
            "  bench     run per-family detect/eval summary\n"
            "  version   print version\n"
            "  help      show this help\n");
}

static void detect_usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator detect --in PATH --out PATH [options]\n\n"
            "required:\n"
            "  --in PATH\n"
            "  --out PATH\n\n"
            "options:\n"
            "  --oov-out PATH | --oov PATH\n"
            "  --preset NAME\n"
            "  --model NAME | --merkmal-system NAME\n"
            "  --iterations N\n"
            "  --lambda X\n"
            "  --threshold X\n"
            "  --threshold-mode NAME\n"
            "  --threshold-min X\n"
            "  --threshold-max X\n"
            "  --threshold-shrink X\n"
            "  --graph-mode NAME\n"
            "  --graph-expansion-mode NAME\n"
            "  --graph-top-k N\n"
            "  --graph-adaptive-margin X\n"
            "  --graph-adaptive-either-max X\n"
            "  --graph-adaptive-ambiguity X\n"
            "  --graph-mutual-boost X\n"
            "  --graph-triangle-boost X\n"
            "  --graph-contrastive-penalty X\n"
            "  --graph-contrastive-floor X\n"
            "  --graph-risk-rank-weight X\n"
            "  --graph-risk-gap-weight X\n"
            "  --graph-unilateral-penalty X\n"
            "  --graph-rival-floor X\n"
            "  --refine-mode NAME\n"
            "  --refine-confidence-threshold X\n"
            "  --refine-bridge-ratio X\n"
            "  --bootstrap N\n"
            "  --bootstrap-sigma X\n"
            "  --seed N\n"
            "  --jobs N\n"
            "  --cluster-scope NAME\n"
            "  --alignment-mode NAME\n"
            "  --class-reduce BOOL\n"
            "  --no-class-reduce\n"
            "  --scorer NAME\n"
            "  --min-pairs N\n"
            "  --max-forms N\n"
            "  --max-lects N\n"
            "  --dirichlet-concentration X\n"
            "  --dirichlet-sharpness X\n"
            "  --dirichlet-scale NAME\n");
}

static void sweep_usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator sweep --in PATH [options]\n\n"
            "options:\n"
            "  --out-tsv PATH\n"
            "  --thresholds LIST  comma list; auto means Otsu\n"
            "  --preset NAME\n"
            "  --iterations N\n"
            "  --bootstrap N\n"
            "  --seed N\n"
            "  --scorer NAME\n"
            "  --min-pairs N\n");
}

static void bench_usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator bench --in PATH --out-tsv PATH [options]\n\n"
            "options:\n"
            "  --families LIST    comma list of Family values\n"
            "  --preset NAME\n"
            "  --threshold X\n"
            "  --iterations N\n"
            "  --bootstrap N\n"
            "  --seed N\n"
            "  --scorer NAME\n"
            "  --min-pairs N\n"
            "  --min-cognacy N\n"
            "  --max-forms N\n"
            "  --max-lects N\n");
}

static void eval_usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator eval --pred PATH --truth PATH [options]\n\n"
            "required:\n"
            "  --pred PATH\n"
            "  --truth PATH\n\n"
            "options:\n"
            "  --truth-col NAME    truth column; currently only Cognacy\n"
            "  --per-concept BOOL  prepend Concept to Cognacy truth labels (default true)\n"
            "  --global-truth      do not prepend Concept to Cognacy truth labels\n");
}

static const char *arg_value_for(const char *command, int argc, char **argv, int *index, const char *arg, const char *name) {
    size_t n = strlen(name);
    if (strncmp(arg, name, n) == 0 && arg[n] == '=') {
        return arg + n + 1;
    }
    if (strcmp(arg, name) == 0) {
        if (*index + 1 >= argc) {
            fprintf(stderr, "%s: %s requires a value\n", command, name);
            return NULL;
        }
        (*index)++;
        return argv[*index];
    }
    return NULL;
}

static const char *detect_arg_value(int argc, char **argv, int *index, const char *arg, const char *name) {
    return arg_value_for("detect", argc, argv, index, arg, name);
}

static const char *eval_arg_value(int argc, char **argv, int *index, const char *arg, const char *name) {
    return arg_value_for("eval", argc, argv, index, arg, name);
}

static const char *sweep_arg_value(int argc, char **argv, int *index, const char *arg, const char *name) {
    return arg_value_for("sweep", argc, argv, index, arg, name);
}

static const char *bench_arg_value(int argc, char **argv, int *index, const char *arg, const char *name) {
    return arg_value_for("bench", argc, argv, index, arg, name);
}

static int is_flag(const char *arg, const char *name) {
    size_t n = strlen(name);
    return strcmp(arg, name) == 0 || (strncmp(arg, name, n) == 0 && arg[n] == '=');
}

static int parse_long_long(const char *value, long long min, long long max, long long *out) {
    char *end = NULL;
    long long parsed;
    errno = 0;
    parsed = strtoll(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < min || parsed > max) {
        return 0;
    }
    *out = parsed;
    return 1;
}

static int parse_uint64_value(const char *value, uint64_t *out) {
    char *end = NULL;
    unsigned long long parsed;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        return 0;
    }
    *out = (uint64_t)parsed;
    return 1;
}

static int parse_double_value(const char *value, double *out) {
    char *end = NULL;
    double parsed;
    errno = 0;
    parsed = strtod(value, &end);
    if (errno != 0 || end == value || *end != '\0') {
        return 0;
    }
    *out = parsed;
    return 1;
}

static int parse_bool_value(const char *value, int *out) {
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 || strcmp(value, "on") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "no") == 0 || strcmp(value, "off") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int set_int_option(const char *name, const char *value, int *field) {
    long long parsed;
    if (!parse_long_long(value, INT_MIN, INT_MAX, &parsed)) {
        fprintf(stderr, "detect: invalid integer for %s: %s\n", name, value);
        return 0;
    }
    *field = (int)parsed;
    return 1;
}

static int set_nonnegative_int_option(const char *name, const char *value, int *field) {
    long long parsed;
    if (!parse_long_long(value, 0, INT_MAX, &parsed)) {
        fprintf(stderr, "detect: invalid non-negative integer for %s: %s\n", name, value);
        return 0;
    }
    *field = (int)parsed;
    return 1;
}

static int set_double_option(const char *name, const char *value, double *field) {
    double parsed;
    if (!parse_double_value(value, &parsed)) {
        fprintf(stderr, "detect: invalid number for %s: %s\n", name, value);
        return 0;
    }
    *field = parsed;
    return 1;
}

static double elapsed_seconds(clock_t start, clock_t end) {
    return (double)(end - start) / (double)CLOCKS_PER_SEC;
}

static char *copy_string(const char *value) {
    size_t len = strlen(value == NULL ? "" : value);
    char *out = (char *)malloc(len + 1);
    if (out == NULL) return NULL;
    memcpy(out, value == NULL ? "" : value, len + 1);
    return out;
}

static int string_in_csv(const char *list, const char *value) {
    const char *p;
    if (list == NULL || list[0] == '\0') return 1;
    p = list;
    while (*p != '\0') {
        const char *start;
        size_t len;
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        start = p;
        while (*p != '\0' && *p != ',') p++;
        len = (size_t)(p - start);
        while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) len--;
        if (strlen(value == NULL ? "" : value) == len && strncmp(start, value == NULL ? "" : value, len) == 0) return 1;
    }
    return 0;
}

static int unique_push_const(const char ***items, size_t *count, const char *value) {
    size_t i;
    const char **next;
    for (i = 0; i < *count; i++) {
        if (strcmp((*items)[i] == NULL ? "" : (*items)[i], value == NULL ? "" : value) == 0) return 1;
    }
    next = (const char **)realloc((void *)*items, (*count + 1) * sizeof(*next));
    if (next == NULL) return 0;
    *items = next;
    (*items)[(*count)++] = value == NULL ? "" : value;
    return 1;
}

static uint64_t count_unique_clusters(const cg_result *result) {
    const char **clusters = NULL;
    size_t count = 0;
    uint64_t i;
    for (i = 0; i < cg_result_row_count(result); i++) {
        const cg_sidecar_row *row = cg_result_row_at(result, i);
        if (row != NULL && !unique_push_const(&clusters, &count, row->cognator_id)) {
            free((void *)clusters);
            return 0;
        }
    }
    free((void *)clusters);
    return (uint64_t)count;
}

static void summarize_records(const cg_records *records, uint64_t *lects, uint64_t *concepts, uint64_t *cognacy) {
    const char **lect_list = NULL;
    const char **concept_list = NULL;
    size_t lect_count = 0;
    size_t concept_count = 0;
    uint64_t cog_count = 0;
    uint64_t i;
    for (i = 0; i < cg_records_count(records); i++) {
        const cg_record *record = cg_records_get(records, i);
        if (record == NULL) continue;
        if (!unique_push_const(&lect_list, &lect_count, record->language_id)) break;
        if (!unique_push_const(&concept_list, &concept_count, record->concept)) break;
        if (record->cognacy != NULL && record->cognacy[0] != '\0') cog_count++;
    }
    free((void *)lect_list);
    free((void *)concept_list);
    *lects = (uint64_t)lect_count;
    *concepts = (uint64_t)concept_count;
    *cognacy = cog_count;
}

static int scan_preset(int argc, char **argv, cg_options *options) {
    int i;
    for (i = 0; i < argc; i++) {
        const char *value = detect_arg_value(argc, argv, &i, argv[i], "--preset");
        if (value != NULL) {
            cg_status status = cg_options_apply_preset(options, value);
            if (status != CG_OK) {
                fprintf(stderr, "detect: unknown preset: %s\n", value);
                return 0;
            }
        }
    }
    return 1;
}

static int parse_detect_args(int argc, char **argv, detect_args *args) {
    int i;
    cg_options_init_defaults(&args->options);
    args->in = NULL;
    args->out = NULL;
    args->oov_out = NULL;
    args->max_forms = 0;
    args->max_lects = 0;
    if (!scan_preset(argc, argv, &args->options)) {
        return 0;
    }
    for (i = 0; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;
        if (strcmp(arg, "help") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            detect_usage(stdout);
            exit(0);
        }
        if ((value = detect_arg_value(argc, argv, &i, arg, "--in")) != NULL) {
            args->in = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--out")) != NULL) {
            args->out = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--oov-out")) != NULL || (value = detect_arg_value(argc, argv, &i, arg, "--oov")) != NULL) {
            args->oov_out = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--preset")) != NULL) {
            (void)value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--model")) != NULL || (value = detect_arg_value(argc, argv, &i, arg, "--merkmal-system")) != NULL) {
            args->options.merkmal_system = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--iterations")) != NULL) {
            if (!set_nonnegative_int_option("--iterations", value, &args->options.iterations)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--lambda")) != NULL) {
            if (!set_double_option("--lambda", value, &args->options.lambda)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--threshold")) != NULL) {
            if (!set_double_option("--threshold", value, &args->options.threshold)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--threshold-mode")) != NULL) {
            args->options.threshold_mode = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--threshold-min")) != NULL) {
            if (!set_double_option("--threshold-min", value, &args->options.threshold_min)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--threshold-max")) != NULL) {
            if (!set_double_option("--threshold-max", value, &args->options.threshold_max)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--threshold-shrink")) != NULL) {
            if (!set_double_option("--threshold-shrink", value, &args->options.threshold_shrink)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-mode")) != NULL) {
            args->options.graph_mode = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-expansion-mode")) != NULL) {
            args->options.graph_expansion_mode = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-top-k")) != NULL) {
            if (!set_nonnegative_int_option("--graph-top-k", value, &args->options.graph_top_k)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-adaptive-margin")) != NULL) {
            if (!set_double_option("--graph-adaptive-margin", value, &args->options.graph_adaptive_margin)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-adaptive-either-max")) != NULL) {
            if (!set_double_option("--graph-adaptive-either-max", value, &args->options.graph_adaptive_either_max)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-adaptive-ambiguity")) != NULL) {
            if (!set_double_option("--graph-adaptive-ambiguity", value, &args->options.graph_adaptive_ambiguity)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-mutual-boost")) != NULL) {
            if (!set_double_option("--graph-mutual-boost", value, &args->options.graph_mutual_boost)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-triangle-boost")) != NULL) {
            if (!set_double_option("--graph-triangle-boost", value, &args->options.graph_triangle_boost)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-contrastive-penalty")) != NULL) {
            if (!set_double_option("--graph-contrastive-penalty", value, &args->options.graph_contrastive_penalty)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-contrastive-floor")) != NULL) {
            if (!set_double_option("--graph-contrastive-floor", value, &args->options.graph_contrastive_floor)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-risk-rank-weight")) != NULL) {
            if (!set_double_option("--graph-risk-rank-weight", value, &args->options.graph_risk_rank_weight)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-risk-gap-weight")) != NULL) {
            if (!set_double_option("--graph-risk-gap-weight", value, &args->options.graph_risk_gap_weight)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-unilateral-penalty")) != NULL) {
            if (!set_double_option("--graph-unilateral-penalty", value, &args->options.graph_unilateral_penalty)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--graph-rival-floor")) != NULL) {
            if (!set_double_option("--graph-rival-floor", value, &args->options.graph_rival_floor)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--refine-mode")) != NULL) {
            args->options.refine_mode = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--refine-confidence-threshold")) != NULL) {
            if (!set_double_option("--refine-confidence-threshold", value, &args->options.refine_confidence_threshold)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--refine-bridge-ratio")) != NULL) {
            if (!set_double_option("--refine-bridge-ratio", value, &args->options.refine_bridge_ratio)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--bootstrap")) != NULL) {
            if (!set_nonnegative_int_option("--bootstrap", value, &args->options.bootstrap)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--bootstrap-sigma")) != NULL) {
            if (!set_double_option("--bootstrap-sigma", value, &args->options.bootstrap_sigma)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--seed")) != NULL) {
            if (!parse_uint64_value(value, &args->options.seed)) {
                fprintf(stderr, "detect: invalid unsigned integer for --seed: %s\n", value);
                return 0;
            }
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--jobs")) != NULL) {
            if (!set_int_option("--jobs", value, &args->options.jobs)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--cluster-scope")) != NULL) {
            args->options.cluster_scope = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--alignment-mode")) != NULL) {
            args->options.alignment_mode = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--class-reduce")) != NULL) {
            if (!parse_bool_value(value, &args->options.class_reduce)) {
                fprintf(stderr, "detect: invalid boolean for --class-reduce: %s\n", value);
                return 0;
            }
        } else if (strcmp(arg, "--no-class-reduce") == 0) {
            args->options.class_reduce = 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--scorer")) != NULL || (value = detect_arg_value(argc, argv, &i, arg, "--scorer-mode")) != NULL) {
            args->options.scorer_mode = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--min-pairs")) != NULL) {
            if (!set_nonnegative_int_option("--min-pairs", value, &args->options.min_pairs)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--max-forms")) != NULL) {
            if (!set_nonnegative_int_option("--max-forms", value, &args->max_forms)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--max-lects")) != NULL) {
            if (!set_nonnegative_int_option("--max-lects", value, &args->max_lects)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--dirichlet-concentration")) != NULL) {
            if (!set_double_option("--dirichlet-concentration", value, &args->options.dirichlet_concentration)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--dirichlet-sharpness")) != NULL) {
            if (!set_double_option("--dirichlet-sharpness", value, &args->options.dirichlet_sharpness)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--dirichlet-scale")) != NULL) {
            args->options.dirichlet_scale_mode = value;
        } else if (is_flag(arg, "--merkmal-dir")) {
            value = detect_arg_value(argc, argv, &i, arg, "--merkmal-dir");
            if (value == NULL) return 0;
            fprintf(stderr, "detect: --merkmal-dir is not used by the C CLI; merkmal is provided by the linked C library\n");
        } else {
            fprintf(stderr, "detect: unknown option: %s\n", arg);
            return 0;
        }
    }
    if (args->in == NULL || args->out == NULL || args->in[0] == '\0' || args->out[0] == '\0') {
        fprintf(stderr, "detect: --in and --out are required\n");
        detect_usage(stderr);
        return 0;
    }
    return 1;
}

static int apply_common_run_option(const char *command, int argc, char **argv, int *i, const char *arg, cg_options *options, int *matched) {
    const char *value = NULL;
    const char *(*value_fn)(int, char **, int *, const char *, const char *) = strcmp(command, "bench") == 0 ? bench_arg_value : sweep_arg_value;
    *matched = 1;
    if ((value = value_fn(argc, argv, i, arg, "--preset")) != NULL) {
        cg_status status = cg_options_apply_preset(options, value);
        if (status != CG_OK) {
            fprintf(stderr, "%s: unknown preset: %s\n", command, value);
            return 0;
        }
    } else if ((value = value_fn(argc, argv, i, arg, "--model")) != NULL || (value = value_fn(argc, argv, i, arg, "--merkmal-system")) != NULL) {
        options->merkmal_system = value;
    } else if ((value = value_fn(argc, argv, i, arg, "--iterations")) != NULL) {
        if (!set_nonnegative_int_option("--iterations", value, &options->iterations)) return 0;
    } else if ((value = value_fn(argc, argv, i, arg, "--threshold")) != NULL) {
        if (!set_double_option("--threshold", value, &options->threshold)) return 0;
    } else if ((value = value_fn(argc, argv, i, arg, "--bootstrap")) != NULL) {
        if (!set_nonnegative_int_option("--bootstrap", value, &options->bootstrap)) return 0;
    } else if ((value = value_fn(argc, argv, i, arg, "--bootstrap-sigma")) != NULL) {
        if (!set_double_option("--bootstrap-sigma", value, &options->bootstrap_sigma)) return 0;
    } else if ((value = value_fn(argc, argv, i, arg, "--seed")) != NULL) {
        if (!parse_uint64_value(value, &options->seed)) {
            fprintf(stderr, "%s: invalid unsigned integer for --seed: %s\n", command, value);
            return 0;
        }
    } else if ((value = value_fn(argc, argv, i, arg, "--scorer")) != NULL || (value = value_fn(argc, argv, i, arg, "--scorer-mode")) != NULL) {
        options->scorer_mode = value;
    } else if ((value = value_fn(argc, argv, i, arg, "--min-pairs")) != NULL) {
        if (!set_nonnegative_int_option("--min-pairs", value, &options->min_pairs)) return 0;
    } else if ((value = value_fn(argc, argv, i, arg, "--graph-mode")) != NULL) {
        options->graph_mode = value;
    } else if ((value = value_fn(argc, argv, i, arg, "--graph-expansion-mode")) != NULL) {
        options->graph_expansion_mode = value;
    } else if ((value = value_fn(argc, argv, i, arg, "--graph-top-k")) != NULL) {
        if (!set_nonnegative_int_option("--graph-top-k", value, &options->graph_top_k)) return 0;
    } else if ((value = value_fn(argc, argv, i, arg, "--refine-mode")) != NULL) {
        options->refine_mode = value;
    } else {
        *matched = 0;
    }
    return 1;
}

static int parse_sweep_args(int argc, char **argv, sweep_args *args) {
    int i;
    cg_options_init_defaults(&args->options);
    args->in = NULL;
    args->out_tsv = NULL;
    args->thresholds = "auto,0.30,0.40,0.50,0.60,0.70,0.80";
    for (i = 0; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;
        int matched = 0;
        if (strcmp(arg, "help") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            sweep_usage(stdout);
            exit(0);
        }
        if ((value = sweep_arg_value(argc, argv, &i, arg, "--in")) != NULL) {
            args->in = value;
        } else if ((value = sweep_arg_value(argc, argv, &i, arg, "--out-tsv")) != NULL) {
            args->out_tsv = value;
        } else if ((value = sweep_arg_value(argc, argv, &i, arg, "--thresholds")) != NULL) {
            args->thresholds = value;
        } else {
            if (!apply_common_run_option("sweep", argc, argv, &i, arg, &args->options, &matched)) return 0;
            if (!matched) {
                fprintf(stderr, "sweep: unknown option: %s\n", arg);
                return 0;
            }
        }
    }
    if (args->in == NULL || args->in[0] == '\0') {
        fprintf(stderr, "sweep: --in is required\n");
        sweep_usage(stderr);
        return 0;
    }
    return 1;
}

static int parse_bench_args(int argc, char **argv, bench_args *args) {
    int i;
    cg_options_init_defaults(&args->options);
    args->in = NULL;
    args->out_tsv = NULL;
    args->families = NULL;
    args->max_forms = 15000;
    args->max_lects = 250;
    args->min_cognacy = 1;
    for (i = 0; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;
        int matched = 0;
        if (strcmp(arg, "help") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            bench_usage(stdout);
            exit(0);
        }
        if ((value = bench_arg_value(argc, argv, &i, arg, "--in")) != NULL) {
            args->in = value;
        } else if ((value = bench_arg_value(argc, argv, &i, arg, "--out-tsv")) != NULL) {
            args->out_tsv = value;
        } else if ((value = bench_arg_value(argc, argv, &i, arg, "--families")) != NULL || (value = bench_arg_value(argc, argv, &i, arg, "--datasets")) != NULL) {
            args->families = value;
        } else if ((value = bench_arg_value(argc, argv, &i, arg, "--max-forms")) != NULL) {
            if (!set_nonnegative_int_option("--max-forms", value, &args->max_forms)) return 0;
        } else if ((value = bench_arg_value(argc, argv, &i, arg, "--max-lects")) != NULL) {
            if (!set_nonnegative_int_option("--max-lects", value, &args->max_lects)) return 0;
        } else if ((value = bench_arg_value(argc, argv, &i, arg, "--min-cognacy")) != NULL) {
            if (!set_nonnegative_int_option("--min-cognacy", value, &args->min_cognacy)) return 0;
        } else {
            if (!apply_common_run_option("bench", argc, argv, &i, arg, &args->options, &matched)) return 0;
            if (!matched) {
                fprintf(stderr, "bench: unknown option: %s\n", arg);
                return 0;
            }
        }
    }
    if (args->in == NULL || args->out_tsv == NULL || args->in[0] == '\0' || args->out_tsv[0] == '\0') {
        fprintf(stderr, "bench: --in and --out-tsv are required\n");
        bench_usage(stderr);
        return 0;
    }
    return 1;
}

static int detect_cmd(int argc, char **argv) {
    detect_args args;
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    cg_result *result = NULL;
    cg_status status;
    int exit_code = 1;

    if (!parse_detect_args(argc, argv, &args)) {
        return 2;
    }
    status = cg_context_new_builtin(&ctx);
    if (status != CG_OK) {
        fprintf(stderr, "detect: initialize context: %s\n", cg_status_string(status));
        goto done;
    }
    status = cg_read_table(args.in, &records);
    if (status != CG_OK) {
        fprintf(stderr, "detect: read %s: %s\n", args.in, cg_status_string(status));
        goto done;
    }
    if (args.max_forms > 0 || args.max_lects > 0) {
        uint64_t lects = 0, concepts = 0, cognacy = 0;
        summarize_records(records, &lects, &concepts, &cognacy);
        (void)concepts;
        (void)cognacy;
        if (args.max_forms > 0 && cg_records_count(records) > (uint64_t)args.max_forms) {
            fprintf(stderr, "detect: input has %llu forms, above --max-forms=%d\n",
                    (unsigned long long)cg_records_count(records), args.max_forms);
            exit_code = 2;
            goto done;
        }
        if (args.max_lects > 0 && lects > (uint64_t)args.max_lects) {
            fprintf(stderr, "detect: input has %llu lects, above --max-lects=%d\n",
                    (unsigned long long)lects, args.max_lects);
            exit_code = 2;
            goto done;
        }
    }
    status = cg_detect(ctx, records, &args.options, &result);
    if (status != CG_OK) {
        fprintf(stderr, "detect: pipeline: %s\n", cg_status_string(status));
        goto done;
    }
    status = cg_write_sidecar(args.out, result);
    if (status != CG_OK) {
        fprintf(stderr, "detect: write %s: %s\n", args.out, cg_status_string(status));
        goto done;
    }
    if (args.oov_out != NULL && args.oov_out[0] != '\0') {
        status = cg_write_oov_report(args.oov_out, result);
        if (status != CG_OK) {
            fprintf(stderr, "detect: write %s: %s\n", args.oov_out, cg_status_string(status));
            goto done;
        }
    }
    fprintf(stderr, "detect: %llu rows, threshold=%.3f, %llu OOV types\n",
            (unsigned long long)cg_result_row_count(result),
            cg_result_threshold(result),
            (unsigned long long)cg_result_oov_count(result));
    exit_code = 0;

done:
    cg_result_free(result);
    cg_records_free(records);
    cg_context_free(ctx);
    return exit_code;
}

static int eval_cmd(int argc, char **argv) {
    const char *pred = NULL;
    const char *truth = NULL;
    const char *truth_col = "Cognacy";
    int per_concept = 1;
    cg_eval_result result;
    cg_status status;
    int i;

    for (i = 0; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;
        if (strcmp(arg, "help") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            eval_usage(stdout);
            return 0;
        }
        if ((value = eval_arg_value(argc, argv, &i, arg, "--pred")) != NULL) {
            pred = value;
        } else if ((value = eval_arg_value(argc, argv, &i, arg, "--truth")) != NULL) {
            truth = value;
        } else if ((value = eval_arg_value(argc, argv, &i, arg, "--truth-col")) != NULL) {
            truth_col = value;
        } else if ((value = eval_arg_value(argc, argv, &i, arg, "--per-concept")) != NULL) {
            if (!parse_bool_value(value, &per_concept)) {
                fprintf(stderr, "eval: invalid boolean for --per-concept: %s\n", value);
                return 2;
            }
        } else if (strcmp(arg, "--global-truth") == 0) {
            per_concept = 0;
        } else {
            fprintf(stderr, "eval: unknown option: %s\n", arg);
            return 2;
        }
    }
    if (pred == NULL || truth == NULL || pred[0] == '\0' || truth[0] == '\0') {
        fprintf(stderr, "eval: --pred and --truth are required\n");
        eval_usage(stderr);
        return 2;
    }
    status = cg_eval_b_cubed(pred, truth, truth_col, per_concept, &result);
    if (status != CG_OK) {
        fprintf(stderr, "eval: %s\n", cg_status_string(status));
        return 1;
    }
    printf("items scored: %llu  (pred=%llu  truth=%llu)\n",
           (unsigned long long)result.scored_count,
           (unsigned long long)result.pred_count,
           (unsigned long long)result.truth_count);
    printf("B-cubed Precision: %.4f\n", result.precision);
    printf("B-cubed Recall:    %.4f\n", result.recall);
    printf("B-cubed F1:        %.4f\n", result.f1);
    return 0;
}

static int sweep_cmd(int argc, char **argv) {
    sweep_args args;
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    FILE *out = NULL;
    char *thresholds = NULL;
    char *tok;
    cg_status status;
    int exit_code = 1;

    if (!parse_sweep_args(argc, argv, &args)) return 2;
    status = cg_context_new_builtin(&ctx);
    if (status != CG_OK) {
        fprintf(stderr, "sweep: initialize context: %s\n", cg_status_string(status));
        goto done;
    }
    status = cg_read_table(args.in, &records);
    if (status != CG_OK) {
        fprintf(stderr, "sweep: read %s: %s\n", args.in, cg_status_string(status));
        goto done;
    }
    out = args.out_tsv == NULL ? stdout : fopen(args.out_tsv, "wb");
    if (out == NULL) {
        fprintf(stderr, "sweep: write %s: I/O error\n", args.out_tsv);
        goto done;
    }
    thresholds = copy_string(args.thresholds);
    if (thresholds == NULL) {
        fprintf(stderr, "sweep: out of memory\n");
        goto done;
    }
    fprintf(out, "threshold_label\tforms\tcognacy\tresolved_threshold\tclusters\tprecision\trecall\tf1\telapsed_ms\n");
    tok = strtok(thresholds, ",");
    while (tok != NULL) {
        cg_options options = args.options;
        cg_result *result = NULL;
        cg_eval_result eval = {0};
        uint64_t lects = 0, concepts = 0, cognacy = 0;
        clock_t start;
        clock_t end;
        char *label = tok;
        while (*label == ' ' || *label == '\t') label++;
        if (strcmp(label, "auto") == 0) {
            options.threshold = 0.0;
        } else {
            double threshold;
            if (!parse_double_value(label, &threshold)) {
                fprintf(stderr, "sweep: skip invalid threshold: %s\n", label);
                tok = strtok(NULL, ",");
                continue;
            }
            options.threshold = threshold;
        }
        summarize_records(records, &lects, &concepts, &cognacy);
        (void)lects;
        (void)concepts;
        start = clock();
        status = cg_detect(ctx, records, &options, &result);
        end = clock();
        if (status == CG_OK) status = cg_eval_b_cubed_result(result, records, "Cognacy", 1, &eval);
        if (status != CG_OK) {
            fprintf(stderr, "sweep: pipeline: %s\n", cg_status_string(status));
            cg_result_free(result);
            goto done;
        }
        fprintf(out, "%s\t%llu\t%llu\t%.6f\t%llu\t%.6f\t%.6f\t%.6f\t%.0f\n",
                label,
                (unsigned long long)cg_records_count(records),
                (unsigned long long)cognacy,
                cg_result_threshold(result),
                (unsigned long long)count_unique_clusters(result),
                eval.precision,
                eval.recall,
                eval.f1,
                elapsed_seconds(start, end) * 1000.0);
        cg_result_free(result);
        tok = strtok(NULL, ",");
    }
    exit_code = 0;

done:
    free(thresholds);
    if (out != NULL && out != stdout) {
        if (fclose(out) != 0 && exit_code == 0) exit_code = 1;
    }
    cg_records_free(records);
    cg_context_free(ctx);
    return exit_code;
}

static int bench_cmd(int argc, char **argv) {
    bench_args args;
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    FILE *out = NULL;
    const char **families = NULL;
    size_t family_count = 0;
    uint64_t i;
    size_t f;
    cg_status status;
    int exit_code = 1;

    if (!parse_bench_args(argc, argv, &args)) return 2;
    status = cg_context_new_builtin(&ctx);
    if (status != CG_OK) {
        fprintf(stderr, "bench: initialize context: %s\n", cg_status_string(status));
        goto done;
    }
    status = cg_read_table(args.in, &records);
    if (status != CG_OK) {
        fprintf(stderr, "bench: read %s: %s\n", args.in, cg_status_string(status));
        goto done;
    }
    for (i = 0; i < cg_records_count(records); i++) {
        const cg_record *record = cg_records_get(records, i);
        if (record == NULL) continue;
        if (!string_in_csv(args.families, record->family)) continue;
        if (!unique_push_const(&families, &family_count, record->family)) {
            fprintf(stderr, "bench: out of memory\n");
            goto done;
        }
    }
    out = fopen(args.out_tsv, "wb");
    if (out == NULL) {
        fprintf(stderr, "bench: write %s: I/O error\n", args.out_tsv);
        goto done;
    }
    fprintf(out, "family\tlects\tconcepts\tforms\tcognacy\toov\tthreshold\tclusters\tprecision\trecall\tf1\telapsed_ms\tskipped\n");
    for (f = 0; f < family_count; f++) {
        cg_records *family_records = NULL;
        cg_result *result = NULL;
        cg_eval_result eval = {0};
        uint64_t lects = 0, concepts = 0, cognacy = 0;
        clock_t start;
        clock_t end;
        const char *skip = "";
        status = cg_records_filter_family(records, families[f], &family_records);
        if (status != CG_OK) {
            fprintf(stderr, "bench: filter %s: %s\n", families[f], cg_status_string(status));
            goto done;
        }
        summarize_records(family_records, &lects, &concepts, &cognacy);
        if (args.min_cognacy > 0 && cognacy < (uint64_t)args.min_cognacy) {
            skip = "too_few_cognacy";
        } else if (args.max_forms > 0 && cg_records_count(family_records) > (uint64_t)args.max_forms) {
            skip = "too_many_forms";
        } else if (args.max_lects > 0 && lects > (uint64_t)args.max_lects) {
            skip = "too_many_lects";
        }
        if (skip[0] != '\0') {
            fprintf(out, "%s\t%llu\t%llu\t%llu\t%llu\t0\t0.000000\t0\t0.000000\t0.000000\t0.000000\t0\t%s\n",
                    families[f],
                    (unsigned long long)lects,
                    (unsigned long long)concepts,
                    (unsigned long long)cg_records_count(family_records),
                    (unsigned long long)cognacy,
                    skip);
            cg_records_free(family_records);
            continue;
        }
        start = clock();
        status = cg_detect(ctx, family_records, &args.options, &result);
        end = clock();
        if (status == CG_OK) status = cg_eval_b_cubed_result(result, family_records, "Cognacy", 1, &eval);
        if (status != CG_OK) {
            fprintf(stderr, "bench: pipeline %s: %s\n", families[f], cg_status_string(status));
            cg_result_free(result);
            cg_records_free(family_records);
            goto done;
        }
        fprintf(out, "%s\t%llu\t%llu\t%llu\t%llu\t%llu\t%.6f\t%llu\t%.6f\t%.6f\t%.6f\t%.0f\t\n",
                families[f],
                (unsigned long long)lects,
                (unsigned long long)concepts,
                (unsigned long long)cg_records_count(family_records),
                (unsigned long long)cognacy,
                (unsigned long long)cg_result_oov_count(result),
                cg_result_threshold(result),
                (unsigned long long)count_unique_clusters(result),
                eval.precision,
                eval.recall,
                eval.f1,
                elapsed_seconds(start, end) * 1000.0);
        cg_result_free(result);
        cg_records_free(family_records);
    }
    exit_code = 0;

done:
    free((void *)families);
    if (out != NULL && fclose(out) != 0 && exit_code == 0) exit_code = 1;
    cg_records_free(records);
    cg_context_free(ctx);
    return exit_code;
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argc >= 2 ? stdout : stderr);
        return argc >= 2 ? 0 : 2;
    }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        if (argc != 2) {
            fprintf(stderr, "version: unexpected argument: %s\n", argv[2]);
            return 2;
        }
        printf("cognator %s\n", cg_version_string());
        return 0;
    }
    if (strcmp(argv[1], "detect") == 0) {
        return detect_cmd(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "eval") == 0) {
        return eval_cmd(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "sweep") == 0) {
        return sweep_cmd(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "bench") == 0) {
        return bench_cmd(argc - 2, argv + 2);
    }
    fprintf(stderr, "cognator: unknown command: %s\n", argv[1]);
    usage(stderr);
    return 2;
}
