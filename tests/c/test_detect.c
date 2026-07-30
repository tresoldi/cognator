#include "cognator.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;

    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    size = ftell(f);
    assert(size >= 0);
    assert(fseek(f, 0, SEEK_SET) == 0);
    buf = (char *)calloc((size_t)size + 1, 1);
    assert(buf != NULL);
    assert(fread(buf, 1, (size_t)size, f) == (size_t)size);
    assert(fclose(f) == 0);
    return buf;
}

static const cg_sidecar_row *find_row(const cg_result *result, const char *id) {
    uint64_t i;

    for (i = 0; i < cg_result_row_count(result); i++) {
        const cg_sidecar_row *row = cg_result_row_at(result, i);
        if (strcmp(row->id, id) == 0) {
            return row;
        }
    }
    return NULL;
}

int main(void) {
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    cg_result *result = NULL;
    cg_options options;
    const cg_sidecar_row *a1;
    const cg_sidecar_row *b1;
    const cg_sidecar_row *c1;
    cg_status status;
    char *text;

    write_text(
        "detect_fixture.tsv",
        "ID\tLanguage_ID\tConcept\tSegments\tFamily\tCognacy\n"
        "a1\tlectA\tC1\tp a k a\tsyn\t1\n"
        "a2\tlectA\tC2\tt u k i\tsyn\t2\n"
        "b1\tlectB\tC1\tp a g a\tsyn\t1\n"
        "b2\tlectB\tC2\tt u g i\tsyn\t2\n"
        "c1\tlectC\tC1\tb a k a\tsyn\t1\n"
        "c2\tlectC\tC2\td u k i\tsyn\t2\n"
        "z1\tlectZ\tC2\tzzz_invalid a\tsyn\t3\n"
    );
    assert(cg_context_new_builtin(&ctx) == CG_OK);
    assert(cg_read_table("detect_fixture.tsv", &records) == CG_OK);
    cg_options_init_defaults(&options);
    options.threshold = 0.70;
    options.bootstrap = 4;
    options.bootstrap_sigma = 0.01;
    options.graph_mode = "support";
    options.graph_expansion_mode = "adaptive";
    options.graph_top_k = 2;
    options.refine_mode = "bridge-cut";
    options.scorer_mode = "posterior";
    options.min_pairs = 1;
    status = cg_detect(ctx, records, &options, &result);
    if (status != CG_OK) {
        fprintf(stderr, "cg_detect: %s\n", cg_status_string(status));
    }
    assert(status == CG_OK);
    assert(cg_result_row_count(result) == cg_records_count(records));
    assert(strcmp(cg_result_row_at(result, 0)->id, "a1") == 0);
    assert(strcmp(cg_result_row_at(result, 1)->id, "a2") == 0);
    assert(strcmp(cg_result_row_at(result, 2)->id, "b1") == 0);
    assert(cg_result_threshold(result) == 0.70);
    assert(cg_result_oov_count(result) == 1);
    assert(strcmp(cg_result_oov_at(result, 0)->grapheme, "zzz_invalid") == 0);

    a1 = find_row(result, "a1");
    b1 = find_row(result, "b1");
    c1 = find_row(result, "c1");
    assert(a1 != NULL && b1 != NULL && c1 != NULL);
    assert(strcmp(a1->cognator_id, b1->cognator_id) == 0);
    assert(strcmp(a1->cognator_id, c1->cognator_id) == 0);
    assert(strstr(a1->cognator_id, "syn:C1:") == a1->cognator_id);
    assert(a1->confidence >= 0.0 && a1->confidence <= 1.0);
    assert(a1->cluster_size == 3);
    assert(a1->bootstrap > 0.0);

    assert(cg_write_sidecar("detect_sidecar.tsv", result) == CG_OK);
    text = read_text("detect_sidecar.tsv");
    assert(strstr(text, "ID\tCognator_ID\tCognator_Confidence") == text);
    assert(strstr(text, "a1\tsyn:C1:") != NULL);
    free(text);
    assert(cg_write_oov_report("detect_oov.tsv", result) == CG_OK);
    text = read_text("detect_oov.tsv");
    assert(strcmp(text, "grapheme\tcount\nzzz_invalid\t1\n") == 0);
    free(text);
    cg_result_free(result);
    result = NULL;

    cg_options_init_defaults(&options);
    options.threshold = 0.0;
    options.threshold_min = 0.2;
    options.threshold_max = 0.8;
    assert(cg_detect(ctx, records, &options, &result) == CG_OK);
    assert(cg_result_threshold(result) >= 0.2 && cg_result_threshold(result) <= 0.8);

    assert(cg_detect(NULL, records, &options, &result) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_detect(ctx, NULL, &options, &result) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_detect(ctx, records, &options, NULL) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_result_row_at(NULL, 0) == NULL);
    assert(cg_result_oov_at(NULL, 0) == NULL);
    assert(cg_write_sidecar("bad.tsv", NULL) == CG_ERR_INVALID_ARGUMENT);

    cg_result_free(result);
    cg_records_free(records);
    cg_context_free(ctx);
    return 0;
}
