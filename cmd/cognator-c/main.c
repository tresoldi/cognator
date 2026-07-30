#include "cognator.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct detect_args {
    const char *in;
    const char *out;
    const char *oov_out;
    cg_options options;
} detect_args;

static void usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator <command> [options]\n\n"
            "commands:\n"
            "  detect    cluster forms into cognate sets\n"
            "  eval      score predicted clusters against Cognacy truth\n"
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
            "  --dirichlet-concentration X\n"
            "  --dirichlet-sharpness X\n"
            "  --dirichlet-scale NAME\n");
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
    fprintf(stderr, "cognator: unknown command: %s\n", argv[1]);
    usage(stderr);
    return 2;
}
