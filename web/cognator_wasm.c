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

static void apply_options(const char *json, cg_options *options) {
    double number;
    int integer;
    cg_options_init_defaults(options);
    if (json != NULL && strstr(json, "\"preset\"") != NULL && strstr(json, "support-adaptive-bridge") != NULL) {
        cg_options_apply_preset(options, "support-adaptive-bridge");
    }
    if (json_number(json, "threshold", &number)) options->threshold = number;
    if (json_number(json, "lambda", &number)) options->lambda = number;
    if (json_int(json, "iterations", &integer)) options->iterations = integer;
    if (json_int(json, "bootstrap", &integer)) options->bootstrap = integer;
    if (json_int(json, "min_pairs", &integer)) options->min_pairs = integer;
    if (json_int(json, "seed", &integer)) options->seed = (uint64_t)integer;
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
    apply_options(options_json, &options);
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
    jb_puts(&buf, ",\"elapsed_ms\":0},\"eval\":{");
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
