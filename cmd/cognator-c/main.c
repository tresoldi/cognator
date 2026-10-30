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
    uint64_t max_pairs;
    cg_options options;
    /* Pairwise export; all unset means a plain cg_detect run. */
    const char *pairs_out;
    const char *pairs_format;
    const char *pairs_unit;
    int morpheme_ids;
    cg_pairs_spec pairs;
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
    uint64_t max_pairs;
    int min_cognacy;
    cg_options options;
} bench_args;

typedef struct diag_args {
    const char *in;
    const char *family;
    int top;
    cg_options options;
} diag_args;

typedef struct oovscan_args {
    const char *in;
    const char *out;
    int top;
    cg_options options;
} oovscan_args;

static void usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator <command> [options]\n\n"
            "commands:\n"
            "  detect    cluster forms into cognate sets\n"
            "  eval      score predicted clusters against Cognacy truth\n"
            "  sweep     run detect/eval across thresholds\n"
            "  bench     run per-family detect/eval summary\n"
            "  diag      print linguistic sanity statistics for a wordlist\n"
            "  oovscan   report graphemes absent from the linked merkmal system\n"
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
            "  --max-pairs N\n"
            "  --dirichlet-concentration X\n"
            "  --dirichlet-sharpness X\n"
            "  --dirichlet-scale NAME\n\n"
            "pairwise export (q(pairs); see docs/pairs.md):\n"
            "  --pairs-out PATH        write weighted pairs; JSON q_pairs if PATH ends in .json\n"
            "  --pairs-format NAME     tsv | json, overriding the extension\n"
            "  --pairs-unit NAME       words (default) | morphemes (split at '+')\n"
            "  --morpheme-ids          add Cognator_Morpheme_IDs to the sidecar (morphemes)\n"
            "  --null-samples N        null pairs drawn per stratum (default 256)\n"
            "  --null-min N            smallest stratum used before widening (default 20)\n"
            "  --null-seed N           seed of the null sample (default 1)\n");
}

static void sweep_usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator sweep --in PATH [options]\n\n"
            "options:\n"
            "  --out-tsv PATH\n"
            "  --thresholds LIST  comma list; auto means Otsu\n"
            "  --preset NAME\n"
            "  --threshold-mode NAME\n"
            "  --threshold-min X\n"
            "  --threshold-max X\n"
            "  --threshold-shrink X\n"
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
            "  --threshold-mode NAME\n"
            "  --threshold-min X\n"
            "  --threshold-max X\n"
            "  --threshold-shrink X\n"
            "  --max-pairs N\n"
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

static void diag_usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator diag --in PATH [options]\n\n"
            "options:\n"
            "  --data PATH         alias for --in\n"
            "  --family NAME\n"
            "  --model NAME | --merkmal-system NAME\n"
            "  --top N             OOV rows to print (default 10)\n");
}

static void oovscan_usage(FILE *stream) {
    fprintf(stream,
            "usage: cognator oovscan --in PATH [options]\n\n"
            "options:\n"
            "  --data PATH         alias for --in\n"
            "  --out PATH          write markdown report instead of stdout\n"
            "  --model NAME | --merkmal-system NAME\n"
            "  --top N             OOV rows to print (default 40)\n");
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

static const char *diag_arg_value(int argc, char **argv, int *index, const char *arg, const char *name) {
    return arg_value_for("diag", argc, argv, index, arg, name);
}

static const char *oovscan_arg_value(int argc, char **argv, int *index, const char *arg, const char *name) {
    return arg_value_for("oovscan", argc, argv, index, arg, name);
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

static uint64_t estimate_cross_lect_pairs(const cg_records *records) {
    uint64_t pairs = 0;
    uint64_t i;
    for (i = 0; i < cg_records_count(records); i++) {
        uint64_t j;
        const cg_record *left = cg_records_get(records, i);
        if (left == NULL) continue;
        for (j = i + 1; j < cg_records_count(records); j++) {
            const cg_record *right = cg_records_get(records, j);
            if (right == NULL) continue;
            if (strcmp(left->concept == NULL ? "" : left->concept, right->concept == NULL ? "" : right->concept) != 0) continue;
            if (strcmp(left->language_id == NULL ? "" : left->language_id, right->language_id == NULL ? "" : right->language_id) == 0) continue;
            if (pairs != UINT64_MAX) pairs++;
        }
    }
    return pairs;
}

static cg_status scan_segments(const cg_context *ctx, const cg_records *records, uint64_t *segments, cg_oov_report **oov_out) {
    cg_oov_report *oov = NULL;
    uint64_t total = 0;
    uint64_t i;
    cg_status status = cg_oov_report_new(&oov);
    if (status != CG_OK) return status;
    for (i = 0; i < cg_records_count(records); i++) {
        const cg_record *record = cg_records_get(records, i);
        cg_tokens *tokens = NULL;
        uint64_t j;
        if (record == NULL) continue;
        status = cg_segment_parse_with_oov(ctx, record->segments, oov, &tokens);
        if (status != CG_OK) {
            cg_tokens_free(tokens);
            cg_oov_report_free(oov);
            return status;
        }
        for (j = 0; j < cg_tokens_count(tokens); j++) {
            const cg_token *token = cg_tokens_get(tokens, j);
            if (token != NULL && !token->boundary) total++;
        }
        cg_tokens_free(tokens);
    }
    *segments = total;
    *oov_out = oov;
    return CG_OK;
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
    args->max_pairs = 0;
    args->pairs_out = NULL;
    args->pairs_format = NULL;
    args->pairs_unit = NULL;
    args->morpheme_ids = 0;
    cg_pairs_spec_init_defaults(&args->pairs);
    args->pairs.unit = CG_PAIRS_NONE;
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
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--max-pairs")) != NULL) {
            if (!parse_uint64_value(value, &args->max_pairs)) {
                fprintf(stderr, "detect: invalid unsigned integer for --max-pairs: %s\n", value);
                return 0;
            }
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--dirichlet-concentration")) != NULL) {
            if (!set_double_option("--dirichlet-concentration", value, &args->options.dirichlet_concentration)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--dirichlet-sharpness")) != NULL) {
            if (!set_double_option("--dirichlet-sharpness", value, &args->options.dirichlet_sharpness)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--dirichlet-scale")) != NULL) {
            args->options.dirichlet_scale_mode = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--pairs-out")) != NULL) {
            args->pairs_out = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--pairs-format")) != NULL) {
            if (strcmp(value, "tsv") != 0 && strcmp(value, "json") != 0) {
                fprintf(stderr, "detect: --pairs-format must be tsv or json: %s\n", value);
                return 0;
            }
            args->pairs_format = value;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--pairs-unit")) != NULL) {
            if (strcmp(value, "words") != 0 && strcmp(value, "morphemes") != 0) {
                fprintf(stderr, "detect: --pairs-unit must be words or morphemes: %s\n", value);
                return 0;
            }
            args->pairs_unit = value;
        } else if (strcmp(arg, "--morpheme-ids") == 0) {
            args->morpheme_ids = 1;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--null-samples")) != NULL) {
            if (!set_nonnegative_int_option("--null-samples", value, &args->pairs.null_samples)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--null-min")) != NULL) {
            if (!set_nonnegative_int_option("--null-min", value, &args->pairs.min_null)) return 0;
        } else if ((value = detect_arg_value(argc, argv, &i, arg, "--null-seed")) != NULL) {
            if (!parse_uint64_value(value, &args->pairs.null_seed)) {
                fprintf(stderr, "detect: invalid unsigned integer for --null-seed: %s\n", value);
                return 0;
            }
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
    if (args->morpheme_ids && args->pairs_unit != NULL && strcmp(args->pairs_unit, "morphemes") != 0) {
        fprintf(stderr, "detect: --morpheme-ids clusters morphemes and needs --pairs-unit morphemes\n");
        return 0;
    }
    if (args->pairs_out == NULL && (args->pairs_format != NULL || (args->pairs_unit != NULL && !args->morpheme_ids))) {
        fprintf(stderr, "detect: --pairs-format and --pairs-unit need --pairs-out\n");
        return 0;
    }
    if (args->pairs_out != NULL || args->morpheme_ids) {
        int morphemes = args->morpheme_ids || (args->pairs_unit != NULL && strcmp(args->pairs_unit, "morphemes") == 0);
        args->pairs.unit = morphemes ? CG_PAIRS_MORPHEMES : CG_PAIRS_WORDS;
        args->pairs.morpheme_clusters = args->morpheme_ids;
    }
    return 1;
}

static int ends_with(const char *s, const char *suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
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
    } else if ((value = value_fn(argc, argv, i, arg, "--threshold-mode")) != NULL) {
        options->threshold_mode = value;
    } else if ((value = value_fn(argc, argv, i, arg, "--threshold-min")) != NULL) {
        if (!set_double_option("--threshold-min", value, &options->threshold_min)) return 0;
    } else if ((value = value_fn(argc, argv, i, arg, "--threshold-max")) != NULL) {
        if (!set_double_option("--threshold-max", value, &options->threshold_max)) return 0;
    } else if ((value = value_fn(argc, argv, i, arg, "--threshold-shrink")) != NULL) {
        if (!set_double_option("--threshold-shrink", value, &options->threshold_shrink)) return 0;
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
    args->max_pairs = 3000000;
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
        } else if ((value = bench_arg_value(argc, argv, &i, arg, "--max-pairs")) != NULL) {
            if (!parse_uint64_value(value, &args->max_pairs)) {
                fprintf(stderr, "bench: invalid unsigned integer for --max-pairs: %s\n", value);
                return 0;
            }
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

static int parse_diag_args(int argc, char **argv, diag_args *args) {
    int i;
    cg_options_init_defaults(&args->options);
    args->in = NULL;
    args->family = NULL;
    args->top = 10;
    for (i = 0; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;
        if (strcmp(arg, "help") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            diag_usage(stdout);
            exit(0);
        }
        if ((value = diag_arg_value(argc, argv, &i, arg, "--in")) != NULL || (value = diag_arg_value(argc, argv, &i, arg, "--data")) != NULL) {
            args->in = value;
        } else if ((value = diag_arg_value(argc, argv, &i, arg, "--family")) != NULL) {
            args->family = value;
        } else if ((value = diag_arg_value(argc, argv, &i, arg, "--model")) != NULL || (value = diag_arg_value(argc, argv, &i, arg, "--merkmal-system")) != NULL) {
            args->options.merkmal_system = value;
        } else if ((value = diag_arg_value(argc, argv, &i, arg, "--top")) != NULL) {
            if (!set_nonnegative_int_option("--top", value, &args->top)) return 0;
        } else {
            fprintf(stderr, "diag: unknown option: %s\n", arg);
            return 0;
        }
    }
    if (args->in == NULL || args->in[0] == '\0') {
        fprintf(stderr, "diag: --in is required\n");
        diag_usage(stderr);
        return 0;
    }
    return 1;
}

static int parse_oovscan_args(int argc, char **argv, oovscan_args *args) {
    int i;
    cg_options_init_defaults(&args->options);
    args->in = NULL;
    args->out = NULL;
    args->top = 40;
    for (i = 0; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;
        if (strcmp(arg, "help") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            oovscan_usage(stdout);
            exit(0);
        }
        if ((value = oovscan_arg_value(argc, argv, &i, arg, "--in")) != NULL || (value = oovscan_arg_value(argc, argv, &i, arg, "--data")) != NULL) {
            args->in = value;
        } else if ((value = oovscan_arg_value(argc, argv, &i, arg, "--out")) != NULL) {
            args->out = value;
        } else if ((value = oovscan_arg_value(argc, argv, &i, arg, "--model")) != NULL || (value = oovscan_arg_value(argc, argv, &i, arg, "--merkmal-system")) != NULL) {
            args->options.merkmal_system = value;
        } else if ((value = oovscan_arg_value(argc, argv, &i, arg, "--top")) != NULL) {
            if (!set_nonnegative_int_option("--top", value, &args->top)) return 0;
        } else {
            fprintf(stderr, "oovscan: unknown option: %s\n", arg);
            return 0;
        }
    }
    if (args->in == NULL || args->in[0] == '\0') {
        fprintf(stderr, "oovscan: --in is required\n");
        oovscan_usage(stderr);
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
    if (args.max_forms > 0 || args.max_lects > 0 || args.max_pairs > 0) {
        uint64_t lects = 0, concepts = 0, cognacy = 0;
        uint64_t estimated_pairs = 0;
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
        if (args.max_pairs > 0) {
            estimated_pairs = estimate_cross_lect_pairs(records);
            if (estimated_pairs > args.max_pairs) {
                fprintf(stderr, "detect: input has %llu same-concept cross-lect pairs, above --max-pairs=%llu\n",
                        (unsigned long long)estimated_pairs,
                        (unsigned long long)args.max_pairs);
                exit_code = 2;
                goto done;
            }
        }
    }
    if (args.pairs.unit != CG_PAIRS_NONE) {
        status = cg_detect_with_pairs(ctx, records, &args.options, &args.pairs, &result);
    } else {
        status = cg_detect(ctx, records, &args.options, &result);
    }
    if (status != CG_OK) {
        fprintf(stderr, "detect: pipeline: %s\n", cg_status_string(status));
        goto done;
    }
    status = args.morpheme_ids ? cg_write_sidecar_morphemes(args.out, result) : cg_write_sidecar(args.out, result);
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
    /* The merkmal build is on this line because it belongs with the resolved
     * threshold: both are what the clusters below depend on, and the threshold
     * is a value on the distance scale the merkmal build defines. A log that
     * records one and not the other cannot be used to reproduce the run. */
    fprintf(stderr,
            "detect: %llu rows, threshold=%.3f, %llu OOV types, merkmal %s (%s)\n",
            (unsigned long long)cg_result_row_count(result),
            cg_result_threshold(result),
            (unsigned long long)cg_result_oov_count(result),
            cg_merkmal_version(),
            cg_merkmal_commit());
    if (args.pairs_out != NULL) {
        int json = args.pairs_format != NULL ? strcmp(args.pairs_format, "json") == 0 : ends_with(args.pairs_out, ".json");
        cg_pairs_summary summary;
        status = cg_write_pairs(args.pairs_out, result, json);
        if (status != CG_OK) {
            fprintf(stderr, "detect: write %s: %s\n", args.pairs_out, cg_status_string(status));
            goto done;
        }
        if (cg_result_pairs_summary(result, &summary) == CG_OK) {
            fprintf(stderr,
                    "detect: %llu %s pairs, mean weight %.3f, %llu null pairs in %llu strata (seed %llu)\n",
                    (unsigned long long)summary.pair_count,
                    summary.unit == CG_PAIRS_MORPHEMES ? "morpheme" : "word",
                    summary.mean_weight,
                    (unsigned long long)summary.null_pairs,
                    (unsigned long long)summary.strata,
                    (unsigned long long)summary.null_seed);
        }
    }
    exit_code = 0;

done:
    cg_result_free(result);
    cg_records_free(records);
    cg_context_free(ctx);
    return exit_code;
}

static int diag_cmd(int argc, char **argv) {
    diag_args args;
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    cg_records *filtered = NULL;
    cg_oov_report *oov = NULL;
    const char *system_name = NULL;
    uint64_t lects = 0, concepts = 0, cognacy = 0, segments = 0;
    uint64_t i;
    cg_status status;
    int exit_code = 1;

    if (!parse_diag_args(argc, argv, &args)) return 2;
    status = cg_context_new_builtin(&ctx);
    if (status != CG_OK) {
        fprintf(stderr, "diag: initialize context: %s\n", cg_status_string(status));
        goto done;
    }
    if (args.options.merkmal_system != NULL) {
        status = cg_context_use_system(ctx, args.options.merkmal_system);
        if (status != CG_OK) {
            fprintf(stderr, "diag: model %s: %s\n", args.options.merkmal_system, cg_status_string(status));
            goto done;
        }
    }
    status = cg_read_table(args.in, &records);
    if (status != CG_OK) {
        fprintf(stderr, "diag: read %s: %s\n", args.in, cg_status_string(status));
        goto done;
    }
    if (args.family != NULL && args.family[0] != '\0') {
        status = cg_records_filter_family(records, args.family, &filtered);
        if (status != CG_OK) {
            fprintf(stderr, "diag: filter %s: %s\n", args.family, cg_status_string(status));
            goto done;
        }
    } else {
        filtered = records;
    }
    summarize_records(filtered, &lects, &concepts, &cognacy);
    status = scan_segments(ctx, filtered, &segments, &oov);
    if (status != CG_OK) {
        fprintf(stderr, "diag: scan segments: %s\n", cg_status_string(status));
        goto done;
    }
    (void)cg_context_system_name(ctx, &system_name);
    printf("rows: %llu  system: %s\n",
           (unsigned long long)cg_records_count(filtered),
           system_name == NULL ? "" : system_name);
    printf("lects: %llu  concepts: %llu  expert-cognate-sets: %llu\n",
           (unsigned long long)lects,
           (unsigned long long)concepts,
           (unsigned long long)cognacy);
    printf("segments: %llu  oov types: %llu\n",
           (unsigned long long)segments,
           (unsigned long long)cg_oov_report_count(oov));
    printf("top OOV graphemes:\n");
    for (i = 0; i < cg_oov_report_count(oov) && i < (uint64_t)args.top; i++) {
        const cg_oov_row *row = cg_oov_report_get(oov, i);
        if (row != NULL) {
            printf("  %-8s %llu\n", row->grapheme, (unsigned long long)row->count);
        }
    }
    exit_code = 0;

done:
    cg_oov_report_free(oov);
    if (filtered != records) cg_records_free(filtered);
    cg_records_free(records);
    cg_context_free(ctx);
    return exit_code;
}

static int oovscan_cmd(int argc, char **argv) {
    oovscan_args args;
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    cg_oov_report *oov = NULL;
    FILE *out = NULL;
    const char *system_name = NULL;
    uint64_t segments = 0;
    uint64_t i;
    cg_status status;
    int exit_code = 1;

    if (!parse_oovscan_args(argc, argv, &args)) return 2;
    status = cg_context_new_builtin(&ctx);
    if (status != CG_OK) {
        fprintf(stderr, "oovscan: initialize context: %s\n", cg_status_string(status));
        goto done;
    }
    if (args.options.merkmal_system != NULL) {
        status = cg_context_use_system(ctx, args.options.merkmal_system);
        if (status != CG_OK) {
            fprintf(stderr, "oovscan: model %s: %s\n", args.options.merkmal_system, cg_status_string(status));
            goto done;
        }
    }
    status = cg_read_table(args.in, &records);
    if (status != CG_OK) {
        fprintf(stderr, "oovscan: read %s: %s\n", args.in, cg_status_string(status));
        goto done;
    }
    status = scan_segments(ctx, records, &segments, &oov);
    if (status != CG_OK) {
        fprintf(stderr, "oovscan: scan segments: %s\n", cg_status_string(status));
        goto done;
    }
    out = args.out == NULL ? stdout : fopen(args.out, "wb");
    if (out == NULL) {
        fprintf(stderr, "oovscan: write %s: I/O error\n", args.out);
        goto done;
    }
    (void)cg_context_system_name(ctx, &system_name);
    fprintf(out, "# OOV graphemes (system: %s)\n\n", system_name == NULL ? "" : system_name);
    fprintf(out, "Segments scanned: %llu  Distinct OOV types: %llu\n\n",
            (unsigned long long)segments,
            (unsigned long long)cg_oov_report_count(oov));
    fprintf(out, "| Grapheme | Count |\n");
    fprintf(out, "|---|---:|\n");
    for (i = 0; i < cg_oov_report_count(oov) && i < (uint64_t)args.top; i++) {
        const cg_oov_row *row = cg_oov_report_get(oov, i);
        if (row != NULL) {
            fprintf(out, "| `%s` | %llu |\n", row->grapheme, (unsigned long long)row->count);
        }
    }
    exit_code = 0;

done:
    if (out != NULL && out != stdout && fclose(out) != 0 && exit_code == 0) exit_code = 1;
    cg_oov_report_free(oov);
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
    fprintf(out, "family\tlects\tconcepts\tforms\tcognacy\testimated_pairs\toov\tthreshold\tclusters\tprecision\trecall\tf1\telapsed_ms\tskipped\n");
    for (f = 0; f < family_count; f++) {
        cg_records *family_records = NULL;
        cg_result *result = NULL;
        cg_eval_result eval = {0};
        uint64_t lects = 0, concepts = 0, cognacy = 0;
        uint64_t estimated_pairs = 0;
        clock_t start;
        clock_t end;
        const char *skip = "";
        status = cg_records_filter_family(records, families[f], &family_records);
        if (status != CG_OK) {
            fprintf(stderr, "bench: filter %s: %s\n", families[f], cg_status_string(status));
            goto done;
        }
        summarize_records(family_records, &lects, &concepts, &cognacy);
        estimated_pairs = estimate_cross_lect_pairs(family_records);
        if (args.min_cognacy > 0 && cognacy < (uint64_t)args.min_cognacy) {
            skip = "too_few_cognacy";
        } else if (args.max_forms > 0 && cg_records_count(family_records) > (uint64_t)args.max_forms) {
            skip = "too_many_forms";
        } else if (args.max_lects > 0 && lects > (uint64_t)args.max_lects) {
            skip = "too_many_lects";
        } else if (args.max_pairs > 0 && estimated_pairs > args.max_pairs) {
            skip = "too_many_pairs";
        }
        if (skip[0] != '\0') {
            fprintf(out, "%s\t%llu\t%llu\t%llu\t%llu\t%llu\t0\t0.000000\t0\t0.000000\t0.000000\t0.000000\t0\t%s\n",
                    families[f],
                    (unsigned long long)lects,
                    (unsigned long long)concepts,
                    (unsigned long long)cg_records_count(family_records),
                    (unsigned long long)cognacy,
                    (unsigned long long)estimated_pairs,
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
        fprintf(out, "%s\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%.6f\t%llu\t%.6f\t%.6f\t%.6f\t%.0f\t\n",
                families[f],
                (unsigned long long)lects,
                (unsigned long long)concepts,
                (unsigned long long)cg_records_count(family_records),
                (unsigned long long)cognacy,
                (unsigned long long)estimated_pairs,
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
        /* The merkmal build is part of the answer to "what produced this":
         * cognator's thresholds are values on its distance scale. */
        printf("merkmal %s (%s)\n", cg_merkmal_version(), cg_merkmal_commit());
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
    if (strcmp(argv[1], "diag") == 0) {
        return diag_cmd(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "oovscan") == 0) {
        return oovscan_cmd(argc - 2, argv + 2);
    }
    fprintf(stderr, "cognator: unknown command: %s\n", argv[1]);
    usage(stderr);
    return 2;
}
