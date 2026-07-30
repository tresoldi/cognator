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

int main(void) {
    cg_context *ctx = NULL;
    cg_tokens *tokens = NULL;
    cg_oov_report *report = NULL;
    const cg_token *tok;
    const cg_oov_row *oov;
    char *text;
    char *reduced = NULL;

    assert(cg_context_new_builtin(&ctx) == CG_OK);

    assert(cg_segment_parse(ctx, "  p   a   -  +  t a ²¹  ", &tokens) == CG_OK);
    assert(cg_tokens_count(tokens) == 5);
    tok = cg_tokens_get(tokens, 0);
    assert(strcmp(tok->grapheme, "p") == 0);
    assert(tok->role == CG_ROLE_C);
    tok = cg_tokens_get(tokens, 1);
    assert(strcmp(tok->grapheme, "a") == 0);
    assert(strcmp(tok->tone, "") == 0);
    tok = cg_tokens_get(tokens, 2);
    assert(tok->boundary == 1);
    tok = cg_tokens_get(tokens, 4);
    assert(strcmp(tok->grapheme, "a") == 0);
    assert(strcmp(tok->tone, "²¹") == 0);
    assert(tok->role == CG_ROLE_V);
    cg_tokens_free(tokens);

    assert(cg_segment_parse(ctx, "²¹ a", &tokens) == CG_OK);
    assert(cg_tokens_count(tokens) == 2);
    tok = cg_tokens_get(tokens, 0);
    assert(strcmp(tok->grapheme, "²¹") == 0);
    assert(tok->role == CG_ROLE_T);
    cg_tokens_free(tokens);

    assert(cg_oov_report_new(&report) == CG_OK);
    assert(cg_segment_parse_with_oov(ctx, "g zzz_invalid", report, &tokens) == CG_OK);
    assert(strcmp(cg_tokens_get(tokens, 0)->grapheme, "ɡ") == 0);
    tok = cg_tokens_get(tokens, 1);
    assert(strcmp(tok->grapheme, "zzz_invalid") == 0);
    assert(tok->oov == 1);
    assert(tok->role == CG_ROLE_UNKNOWN);
    assert(cg_oov_report_count(report) == 1);
    assert(strcmp(cg_oov_report_get(report, 0)->grapheme, "zzz_invalid") == 0);
    cg_tokens_free(tokens);
    cg_oov_report_free(report);

    assert(cg_oov_report_new(&report) == CG_OK);
    assert(cg_oov_report_add(report, "zzz_invalid") == CG_OK);
    assert(cg_oov_report_add(report, "zzz_invalid") == CG_OK);
    assert(cg_oov_report_add(report, "aaa_invalid") == CG_OK);
    assert(cg_oov_report_count(report) == 2);
    oov = cg_oov_report_get(report, 0);
    assert(strcmp(oov->grapheme, "aaa_invalid") == 0);
    assert(oov->count == 1);
    oov = cg_oov_report_get(report, 1);
    assert(strcmp(oov->grapheme, "zzz_invalid") == 0);
    assert(oov->count == 2);
    assert(cg_write_oov_report_rows("test_oov.tsv", cg_oov_report_get(report, 0), cg_oov_report_count(report)) == CG_OK);
    text = read_text("test_oov.tsv");
    assert(strcmp(text, "grapheme\tcount\naaa_invalid\t1\nzzz_invalid\t2\n") == 0);
    free(text);
    cg_oov_report_free(report);

    assert(cg_class_reduce("tʰ", &reduced) == CG_OK);
    assert(strcmp(reduced, "t") == 0);
    cg_string_free(reduced);
    assert(cg_class_reduce("kʷ", &reduced) == CG_OK);
    assert(strcmp(reduced, "k") == 0);
    cg_string_free(reduced);
    assert(cg_class_reduce("aː", &reduced) == CG_OK);
    assert(strcmp(reduced, "a") == 0);
    cg_string_free(reduced);
    assert(cg_class_reduce("ã", &reduced) == CG_OK);
    assert(strcmp(reduced, "a") == 0);
    cg_string_free(reduced);
    assert(cg_class_reduce("ⁿg", &reduced) == CG_OK);
    assert(strcmp(reduced, "g") == 0);
    cg_string_free(reduced);
    assert(cg_class_reduce("⁴²", &reduced) == CG_OK);
    assert(strcmp(reduced, "⁴²") == 0);
    cg_string_free(reduced);

    assert(cg_segment_parse(NULL, "p a", &tokens) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_segment_parse(ctx, NULL, &tokens) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_segment_parse(ctx, "p a", NULL) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_tokens_get(NULL, 0) == NULL);
    cg_tokens_free(NULL);
    cg_context_free(ctx);
    return 0;
}
