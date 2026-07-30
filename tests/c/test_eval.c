#include "cognator.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

static int close_double(double a, double b) {
    return fabs(a - b) < 1e-9;
}

int main(void) {
    cg_eval_result result;

    write_text(
        "eval_truth.tsv",
        "ID\tLanguage_ID\tConcept\tSegments\tFamily\tCognacy\n"
        "a\tL1\tC1\tp a\tsyn\tX\n"
        "b\tL2\tC1\tp a\tsyn\tX\n"
        "c\tL1\tC1\tt a\tsyn\tY\n"
        "d\tL2\tC1\tt a\tsyn\tY\n"
        "e\tL3\tC2\tm a\tsyn\tX\n"
    );
    write_text(
        "eval_pred_identity.tsv",
        "ID\tCognator_ID\n"
        "a\tC1:X\n"
        "b\tC1:X\n"
        "c\tC1:Y\n"
        "d\tC1:Y\n"
        "e\tC2:X\n"
    );
    assert(cg_eval_b_cubed("eval_pred_identity.tsv", "eval_truth.tsv", "Cognacy", 1, &result) == CG_OK);
    assert(result.scored_count == 5);
    assert(result.pred_count == 5);
    assert(result.truth_count == 5);
    assert(close_double(result.precision, 1.0));
    assert(close_double(result.recall, 1.0));
    assert(close_double(result.f1, 1.0));

    write_text(
        "eval_pred_all_one.tsv",
        "ID\tCognator_ID\n"
        "a\tM\n"
        "b\tM\n"
        "c\tM\n"
        "d\tM\n"
    );
    assert(cg_eval_b_cubed("eval_pred_all_one.tsv", "eval_truth.tsv", "Cognacy", 1, &result) == CG_OK);
    assert(result.scored_count == 4);
    assert(result.pred_count == 4);
    assert(result.truth_count == 5);
    assert(close_double(result.precision, 0.5));
    assert(close_double(result.recall, 1.0));
    assert(result.f1 > 0.0 && result.f1 < 1.0);

    write_text(
        "eval_pred_singletons.tsv",
        "ID\tCognator_ID\n"
        "a\t1\n"
        "b\t2\n"
        "c\t3\n"
        "d\t4\n"
    );
    assert(cg_eval_b_cubed("eval_pred_singletons.tsv", "eval_truth.tsv", "Cognacy", 1, &result) == CG_OK);
    assert(close_double(result.precision, 1.0));
    assert(close_double(result.recall, 0.5));

    write_text(
        "eval_pred_partial.tsv",
        "ID\tCognator_ID\n"
        "a\tC1:X\n"
        "extra\toutside\n"
    );
    assert(cg_eval_b_cubed("eval_pred_partial.tsv", "eval_truth.tsv", "Cognacy", 1, &result) == CG_OK);
    assert(result.scored_count == 1);
    assert(result.pred_count == 2);
    assert(result.truth_count == 5);
    assert(close_double(result.precision, 1.0));
    assert(close_double(result.recall, 0.5));

    assert(cg_eval_b_cubed("eval_pred_partial.tsv", "eval_truth.tsv", "Other", 1, &result) == CG_ERR_UNSUPPORTED_OPTION);
    assert(cg_eval_b_cubed(NULL, "eval_truth.tsv", "Cognacy", 1, &result) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_eval_b_cubed("eval_pred_partial.tsv", NULL, "Cognacy", 1, &result) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_eval_b_cubed("eval_pred_partial.tsv", "eval_truth.tsv", "Cognacy", 1, NULL) == CG_ERR_INVALID_ARGUMENT);
    return 0;
}
