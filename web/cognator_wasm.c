#include "cognator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

typedef struct json_buf {
    char *data;
    size_t len;
    size_t cap;
} json_buf;

static int jb_grow(json_buf *buf, size_t add) {
    char *next;
    size_t next_cap;
    if (buf->len + add + 1 <= buf->cap) return 1;
    next_cap = buf->cap == 0 ? 4096 : buf->cap * 2;
    while (next_cap < buf->len + add + 1) next_cap *= 2;
    next = (char *)realloc(buf->data, next_cap);
    if (next == NULL) return 0;
    buf->data = next;
    buf->cap = next_cap;
    return 1;
}

static int jb_puts(json_buf *buf, const char *text) {
    size_t len = strlen(text);
    if (!jb_grow(buf, len)) return 0;
    memcpy(buf->data + buf->len, text, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 1;
}

static int jb_printf(json_buf *buf, const char *fmt, double value) {
    char tmp[128];
    int n = snprintf(tmp, sizeof(tmp), fmt, value);
    if (n < 0 || (size_t)n >= sizeof(tmp)) return 0;
    return jb_puts(buf, tmp);
}

static int jb_put_json_string(json_buf *buf, const char *text) {
    const unsigned char *p = (const unsigned char *)(text == NULL ? "" : text);
    if (!jb_puts(buf, "\"")) return 0;
    while (*p != '\0') {
        char tmp[8];
        if (*p == '"' || *p == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)*p;
            tmp[2] = '\0';
            if (!jb_puts(buf, tmp)) return 0;
        } else if (*p == '\n') {
            if (!jb_puts(buf, "\\n")) return 0;
        } else if (*p == '\r') {
            if (!jb_puts(buf, "\\r")) return 0;
        } else if (*p == '\t') {
            if (!jb_puts(buf, "\\t")) return 0;
        } else if (*p < 0x20) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned int)*p);
            if (!jb_puts(buf, tmp)) return 0;
        } else {
            if (!jb_grow(buf, 1)) return 0;
            buf->data[buf->len++] = (char)*p;
            buf->data[buf->len] = '\0';
        }
        p++;
    }
    return jb_puts(buf, "\"");
}

static const char *json_find_value(const char *json, const char *key) {
    const char *p;
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json == NULL ? "" : json, needle);
    if (p == NULL) return NULL;
    p = strchr(p + strlen(needle), ':');
    if (p == NULL) return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static int json_number(const char *json, const char *key, double *out) {
    const char *p = json_find_value(json, key);
    char *end = NULL;
    double value;
    if (p == NULL) return 0;
    value = strtod(p, &end);
    if (end == p) return 0;
    *out = value;
    return 1;
}

static int json_int(const char *json, const char *key, int *out) {
    double value;
    if (!json_number(json, key, &value)) return 0;
    *out = (int)value;
    return 1;
}

static int json_string_is(const char *json, const char *key, const char *value) {
    const char *p = json_find_value(json, key);
    size_t len = strlen(value);
    if (p == NULL || *p != '"') return 0;
    p++;
    return strncmp(p, value, len) == 0 && p[len] == '"';
}

static int json_bool(const char *json, const char *key, int *out) {
    const char *p = json_find_value(json, key);
    if (p == NULL) return 0;
    if (strncmp(p, "true", 4) == 0) { *out = 1; return 1; }
    if (strncmp(p, "false", 5) == 0) { *out = 0; return 1; }
    return 0;
}

/* Whether the key is present with a string value, empty or not. Distinguishes
 * "the page did not send this" from "the page sent something we do not know",
 * which is the difference between a default and a mistake. */
static int json_has_string(const char *json, const char *key) {
    const char *p = json_find_value(json, key);
    return p != NULL && *p == '"';
}

/* Resolves one enumerated option against the values the library accepts.
 *
 * Assigns the matching literal rather than a copy of the caller's bytes: the
 * cg_options fields are `const char *` that must outlive the call, and a
 * pointer into the request JSON does not. Matching also validates, which is
 * the point -- an unrecognized value returns non-zero so the caller can say so
 * instead of running with a silent default, which is how a page full of
 * controls came to do nothing at all.
 */
static int pick_enum(
    const char *json,
    const char *key,
    const char *const *allowed,
    size_t count,
    const char **out
) {
    size_t i;
    if (!json_has_string(json, key)) return 0;
    /* An empty string is the page's way of saying "leave the default". */
    if (json_string_is(json, key, "")) return 0;
    for (i = 0; i < count; i++) {
        if (json_string_is(json, key, allowed[i])) {
            *out = allowed[i];
            return 0;
        }
    }
    return 1;
}

/* Returns NULL on success, or the name of the option that was not understood.
 *
 * Every field the browser page exposes a control for is read here. It used to
 * read eleven of them and drop the rest on the floor, so twenty-seven controls
 * on the published page changed nothing at all: a visitor could set the graph
 * policy, the scorer, the refinement or any Dirichlet parameter, press Run and
 * get byte-identical output. tests/c/test_wasm_options.cmake fails if app.js
 * grows a knob this function does not name. */
static const char *apply_options(const char *json, cg_options *options) {
    static const char *const threshold_modes[] = {"family", "concept-shrink"};
    static const char *const graph_modes[] = {"support", "threshold"};
    static const char *const expansion_modes[] = {"adaptive", "mutual", "either"};
    static const char *const refine_modes[] = {"bridge-cut", "none"};
    static const char *const cluster_scopes[] = {"concept", "global"};
    static const char *const alignment_modes[] = {"combined", "global"};
    static const char *const null_modes[] = {"marginal", "shuffle"};
    static const char *const scorer_modes[] = {"learned", "posterior"};
    static const char *const dirichlet_scales[] = {"sparse-boost", "rich-boost"};

    struct number_option {
        const char *key;
        double *field;
    };
    struct int_option {
        const char *key;
        int *field;
    };

    double number;
    int integer;
    size_t i;

    cg_options_init_defaults(options);

    /* The preset first, so an explicit knob below overrides what it set --
     * which is what the panel's own reset-on-preset-change behaviour assumes.
     * Matched properly rather than by looking for the preset's name anywhere
     * in the request, which is what this did before: a wordlist containing the
     * string "support-adaptive-bridge" applied the preset. */
    {
        static const char *const presets[] = {"support-adaptive-bridge", "default"};
        const char *preset = NULL;
        if (pick_enum(json, "preset", presets, sizeof presets / sizeof *presets, &preset)) {
            return "preset";
        }
        if (preset != NULL && cg_options_apply_preset(options, preset) != CG_OK) {
            return "preset";
        }
    }

    {
        const struct number_option numbers[] = {
            {"threshold", &options->threshold},
            {"threshold_min", &options->threshold_min},
            {"threshold_max", &options->threshold_max},
            {"threshold_shrink", &options->threshold_shrink},
            {"lambda", &options->lambda},
            {"graph_adaptive_margin", &options->graph_adaptive_margin},
            {"graph_adaptive_either_max", &options->graph_adaptive_either_max},
            {"graph_adaptive_ambiguity", &options->graph_adaptive_ambiguity},
            {"graph_mutual_boost", &options->graph_mutual_boost},
            {"graph_triangle_boost", &options->graph_triangle_boost},
            {"graph_contrastive_penalty", &options->graph_contrastive_penalty},
            {"graph_contrastive_floor", &options->graph_contrastive_floor},
            {"graph_risk_rank_weight", &options->graph_risk_rank_weight},
            {"graph_risk_gap_weight", &options->graph_risk_gap_weight},
            {"graph_unilateral_penalty", &options->graph_unilateral_penalty},
            {"graph_rival_floor", &options->graph_rival_floor},
            {"refine_confidence_threshold", &options->refine_confidence_threshold},
            {"refine_bridge_ratio", &options->refine_bridge_ratio},
            {"bootstrap_sigma", &options->bootstrap_sigma},
            {"dirichlet_concentration", &options->dirichlet_concentration},
            {"dirichlet_sharpness", &options->dirichlet_sharpness},
        };
        for (i = 0; i < sizeof numbers / sizeof *numbers; i++) {
            if (json_number(json, numbers[i].key, &number)) *numbers[i].field = number;
        }
    }

    {
        const struct int_option integers[] = {
            {"iterations", &options->iterations},
            {"bootstrap", &options->bootstrap},
            {"min_pairs", &options->min_pairs},
            {"graph_top_k", &options->graph_top_k},
            {"null_max_runs", &options->null_max_runs},
        };
        for (i = 0; i < sizeof integers / sizeof *integers; i++) {
            if (json_int(json, integers[i].key, &integer)) *integers[i].field = integer;
        }
    }

    if (json_int(json, "seed", &integer)) options->seed = (uint64_t)integer;
    if (json_bool(json, "class_reduce", &integer)) options->class_reduce = integer;

#define PICK(key, table, field)                                                   \
    if (pick_enum(json, key, table, sizeof table / sizeof *table, &options->field)) { \
        return key;                                                               \
    }
    PICK("threshold_mode", threshold_modes, threshold_mode)
    PICK("graph_mode", graph_modes, graph_mode)
    PICK("graph_expansion_mode", expansion_modes, graph_expansion_mode)
    PICK("refine_mode", refine_modes, refine_mode)
    PICK("cluster_scope", cluster_scopes, cluster_scope)
    PICK("alignment_mode", alignment_modes, alignment_mode)
    PICK("null_mode", null_modes, null_mode)
    PICK("scorer_mode", scorer_modes, scorer_mode)
    /* The page calls this dirichlet_scale; the field is dirichlet_scale_mode,
     * and so is the CLI's --dirichlet-scale. Named here, once. */
    PICK("dirichlet_scale", dirichlet_scales, dirichlet_scale_mode)
#undef PICK

    return NULL;
}

static char *error_json(const char *where, cg_status status) {
    json_buf buf = {0};
    jb_puts(&buf, "{\"ok\":false,\"error\":");
    jb_put_json_string(&buf, where);
    jb_puts(&buf, ",\"status\":");
    jb_put_json_string(&buf, cg_status_string(status));
    jb_puts(&buf, "}");
    return buf.data;
}

static int write_input_file(const char *csv_text, const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return 0;
    if (csv_text != NULL && fputs(csv_text, f) < 0) {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}

EMSCRIPTEN_KEEPALIVE
const char *cognator_system(void) {
    return "builtin";
}

/* For the page footer, which has no result to read the provenance out of until
 * someone runs a detection. Static storage: the caller reads it through
 * UTF8ToString and never frees it. */
EMSCRIPTEN_KEEPALIVE
const char *cognator_merkmal(void) {
    static char text[96];
    snprintf(text, sizeof(text), "%s (%s)",
             cg_merkmal_version(), cg_merkmal_commit());
    return text;
}

EMSCRIPTEN_KEEPALIVE
void cognator_free(char *value) {
    free(value);
}

EMSCRIPTEN_KEEPALIVE
char *cognator_detect_json(const char *csv_text, const char *options_json) {
    const char *input_path = "/tmp/cognator_input.csv";
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    cg_result *result = NULL;
    cg_eval_result eval = {0};
    cg_options options;
    cg_status status;
    json_buf buf = {0};
    uint64_t i;

    if (!write_input_file(csv_text == NULL ? "" : csv_text, input_path)) {
        return error_json("write input", CG_ERR_IO);
    }
    {
        /* An option the shim does not understand is reported, not ignored.
         * Silently dropping one is how the page's controls came to be
         * decorative. */
        const char *bad = apply_options(options_json, &options);
        if (bad != NULL) {
            char where[96];
            snprintf(where, sizeof(where), "option: %s", bad);
            return error_json(where, CG_ERR_INVALID_ARGUMENT);
        }
    }
    status = cg_context_new_builtin(&ctx);
    if (status != CG_OK) return error_json("context", status);
    status = cg_read_table(input_path, &records);
    if (status != CG_OK) {
        cg_context_free(ctx);
        return error_json("read", status);
    }
    status = cg_detect(ctx, records, &options, &result);
    if (status != CG_OK) {
        cg_records_free(records);
        cg_context_free(ctx);
        return error_json("detect", status);
    }
    cg_eval_b_cubed_result(result, records, "Cognacy", 1, &eval);

    jb_puts(&buf, "{\"ok\":true,\"summary\":{\"rows\":");
    {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)cg_result_row_count(result));
        jb_puts(&buf, tmp);
    }
    jb_puts(&buf, ",\"elapsed_ms\":0,\"merkmal\":{\"version\":");
    jb_put_json_string(&buf, cg_merkmal_version());
    jb_puts(&buf, ",\"commit\":");
    jb_put_json_string(&buf, cg_merkmal_commit());
    jb_puts(&buf, "}},\"eval\":{");
    jb_puts(&buf, "\"precision\":");
    jb_printf(&buf, "%.8f", eval.precision);
    jb_puts(&buf, ",\"recall\":");
    jb_printf(&buf, "%.8f", eval.recall);
    jb_puts(&buf, ",\"f1\":");
    jb_printf(&buf, "%.8f", eval.f1);
    jb_puts(&buf, "},\"predictions\":[");
    for (i = 0; i < cg_result_row_count(result); i++) {
        const cg_sidecar_row *row = cg_result_row_at(result, i);
        if (i > 0) jb_puts(&buf, ",");
        jb_puts(&buf, "{\"eval_id\":");
        jb_put_json_string(&buf, row->id);
        jb_puts(&buf, ",\"pred\":");
        jb_put_json_string(&buf, row->cognator_id);
        jb_puts(&buf, ",\"confidence\":");
        jb_printf(&buf, "%.8f", row->confidence);
        jb_puts(&buf, ",\"assigned_support\":");
        jb_printf(&buf, "%.8f", row->assigned_support);
        jb_puts(&buf, ",\"alternative_support\":");
        jb_printf(&buf, "%.8f", row->alternative_support);
        jb_puts(&buf, ",\"support_margin\":");
        jb_printf(&buf, "%.8f", row->support_margin);
        jb_puts(&buf, ",\"cluster_size\":");
        {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%d", row->cluster_size);
            jb_puts(&buf, tmp);
        }
        jb_puts(&buf, "}");
    }
    jb_puts(&buf, "]}");

    cg_result_free(result);
    cg_records_free(records);
    cg_context_free(ctx);
    return buf.data;
}
