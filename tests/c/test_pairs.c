/* The pairwise export (q(pairs)) and per-morpheme partial cognacy.
 *
 * argv[1] is tests/fixtures/linguistic/partial_cognacy_compounds.tsv: three
 * lects, compounds whose first morpheme is cognate everywhere and whose second
 * is replaced in Cumo, simplex roots with a shared two-segment suffix `n a`,
 * and two words without boundaries. */
#include "cognator.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

static const cg_pair_row *find_pair(const cg_result *result, const char *a, uint64_t ma, const char *b, uint64_t mb) {
    uint64_t i;
    for (i = 0; i < cg_result_pair_count(result); i++) {
        const cg_pair_row *p = cg_result_pair_at(result, i);
        if (strcmp(p->form_a, a) == 0 && p->morph_a == ma && strcmp(p->form_b, b) == 0 && p->morph_b == mb) {
            return p;
        }
    }
    return NULL;
}

static const char *lect_of(const cg_records *records, const char *id) {
    uint64_t i;
    for (i = 0; i < cg_records_count(records); i++) {
        const cg_record *r = cg_records_get(records, i);
        if (strcmp(r->id, id) == 0) return r->language_id;
    }
    return NULL;
}

static uint64_t morphemes_of(const cg_records *records, const char *id) {
    uint64_t i;
    for (i = 0; i < cg_records_count(records); i++) {
        const cg_record *r = cg_records_get(records, i);
        if (strcmp(r->id, id) == 0) {
            return strstr(r->segments, "+") != NULL ? 2 : 1;
        }
    }
    return 0;
}

static void assert_same_rows(const cg_result *a, const cg_result *b) {
    uint64_t i;
    assert(cg_result_row_count(a) == cg_result_row_count(b));
    assert(cg_result_threshold(a) == cg_result_threshold(b));
    for (i = 0; i < cg_result_row_count(a); i++) {
        const cg_sidecar_row *x = cg_result_row_at(a, i);
        const cg_sidecar_row *y = cg_result_row_at(b, i);
        assert(strcmp(x->id, y->id) == 0);
        assert(strcmp(x->cognator_id, y->cognator_id) == 0);
        assert(x->confidence == y->confidence);
        assert(x->assigned_support == y->assigned_support);
        assert(x->alternative_support == y->alternative_support);
        assert(x->cluster_size == y->cluster_size);
    }
}

static void options_for_test(cg_options *options) {
    assert(cg_options_apply_preset(options, "support-adaptive-bridge") == CG_OK);
    options->threshold = 0.0;
    options->min_pairs = 1;
    options->scorer_mode = "posterior";
}

static uint64_t split_segments(const char *segments, char *buf, size_t cap, const char **items) {
    uint64_t n = 0;
    char *tok;
    char *save = NULL;
    assert(strlen(segments) < cap);
    strcpy(buf, segments);
    for (tok = strtok_r(buf, " ", &save); tok != NULL; tok = strtok_r(NULL, " ", &save)) {
        items[n++] = tok;
    }
    return n;
}

static const cg_record *record_by_id(const cg_records *records, const char *id) {
    uint64_t i;
    for (i = 0; i < cg_records_count(records); i++) {
        if (strcmp(cg_records_get(records, i)->id, id) == 0) return cg_records_get(records, i);
    }
    return NULL;
}

int main(int argc, char **argv) {
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    cg_result *plain = NULL;
    cg_result *words = NULL;
    cg_result *morphs = NULL;
    cg_result *again = NULL;
    cg_options options;
    cg_pairs_spec spec;
    cg_pairs_summary summary;
    cg_pair_row *copied = NULL;
    uint64_t copied_count = 0;
    const cg_pair_row *p;
    const cg_pair_row *q;
    char *text = NULL;
    char *text2 = NULL;
    uint64_t i;

    assert(argc == 2);
    assert(cg_context_new_builtin(&ctx) == CG_OK);
    assert(cg_read_table(argv[1], &records) == CG_OK);
    cg_options_init_defaults(&options);
    options_for_test(&options);

    /* Clustering is untouched by the export, in either unit. */
    assert(cg_detect(ctx, records, &options, &plain) == CG_OK);
    assert(cg_result_pair_count(plain) == 0);
    assert(cg_result_morpheme_ids_at(plain, 0) == NULL);
    assert(cg_result_pairs_summary(plain, &summary) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_result_pairs_tsv(plain, &text) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_detect_with_pairs(ctx, records, &options, NULL, &again) == CG_OK);
    assert_same_rows(plain, again);
    assert(cg_result_pair_count(again) == 0);
    cg_result_free(again);
    again = NULL;

    cg_pairs_spec_init_defaults(&spec);
    assert(spec.unit == CG_PAIRS_WORDS && spec.null_samples == 256 && spec.min_null == 20 && spec.null_seed == 1);
    assert(cg_detect_with_pairs(ctx, records, &options, &spec, &words) == CG_OK);
    assert_same_rows(plain, words);
    /* 14 concepts x 3 lect pairs, one pair per lect pair. */
    assert(cg_result_pair_count(words) == 42);
    for (i = 0; i < cg_result_pair_count(words); i++) {
        p = cg_result_pair_at(words, i);
        assert(p->morph_a == 0 && p->morph_b == 0);
    }

    spec.unit = CG_PAIRS_MORPHEMES;
    spec.morpheme_clusters = 1;
    assert(cg_detect_with_pairs(ctx, records, &options, &spec, &morphs) == CG_OK);
    assert_same_rows(plain, morphs);
    /* 12 two-morpheme concepts x 3 lect pairs x 4, plus 2 simplex x 3. */
    assert(cg_result_pair_count(morphs) == 12 * 3 * 4 + 2 * 3);

    for (i = 0; i < cg_result_pair_count(morphs); i++) {
        p = cg_result_pair_at(morphs, i);
        /* Never a morpheme against its own word, nor within one lect. */
        assert(strcmp(p->form_a, p->form_b) != 0);
        assert(strcmp(lect_of(records, p->form_a), lect_of(records, p->form_b)) != 0);
        assert(p->weight >= 0.0 && p->weight <= 1.0);
        assert(p->distance >= 0.0 && p->distance <= 1.0);
        assert(p->morph_a < morphemes_of(records, p->form_a));
        assert(p->morph_b < morphemes_of(records, p->form_b));
        assert(p->null_level == CG_NULL_STRATUM || p->null_count >= 1);
    }

    /* The shared first morpheme is strong; the replaced second is not. */
    p = find_pair(morphs, "a_pebble", 0, "b_pebble", 0);
    assert(p != NULL && p->weight >= 0.9);
    p = find_pair(morphs, "a_pebble", 0, "c_pebble", 0);
    assert(p != NULL && p->weight >= 0.9);
    p = find_pair(morphs, "a_pebble", 1, "b_pebble", 1);       /* keris ~ geris */
    assert(p != NULL && p->weight >= 0.9);
    p = find_pair(morphs, "a_pebble", 1, "c_pebble", 1);       /* keris ~ wona */
    assert(p != NULL && p->weight <= 0.1);
    p = find_pair(morphs, "a_pebble", 0, "b_pebble", 1);       /* malut ~ geris */
    assert(p != NULL && p->weight <= 0.1);
    p = find_pair(morphs, "a_moon", 0, "c_moon", 0);           /* tisalo ~ oreva */
    assert(p != NULL && p->weight < find_pair(morphs, "a_moon", 0, "b_moon", 0)->weight);

    /* Equal distance, unequal evidence: the identical suffix `n a` matches
     * the background as often as it matches here, the identical root does
     * not. */
    p = find_pair(morphs, "a_water", 1, "b_water", 1);         /* n a ~ n a */
    q = find_pair(morphs, "a_water", 0, "b_water", 0);         /* m o r e v ~ m o r e v */
    assert(p != NULL && q != NULL);
    assert(fabs(p->distance - q->distance) < 1e-12);
    assert(p->weight < q->weight);
    assert(p->weight <= 0.1 && q->weight >= 0.9);
    assert(p->null_level == CG_NULL_STRATUM && p->null_count >= 20);

    /* Morpheme clusters: the replaced morpheme is in no cross-lect set. */
    for (i = 0; i < cg_records_count(records); i++) {
        const char *ids = cg_result_morpheme_ids_at(morphs, i);
        const cg_record *r = cg_records_get(records, i);
        uint64_t tokens = 1;
        const char *c;
        assert(ids != NULL);
        for (c = ids; *c; c++) if (*c == ' ') tokens++;
        assert(tokens == morphemes_of(records, r->id));
        if (strcmp(r->id, "c_pebble") == 0) {
            assert(strcmp(ids, "toy:PEBBLE:m0 -") == 0 || strcmp(ids, "toy:PEBBLE:m1 -") == 0);
        }
    }
    assert(strcmp(cg_result_morpheme_ids_at(morphs, 0), cg_result_morpheme_ids_at(morphs, 1)) == 0);  /* a_pebble, b_pebble */

    assert(cg_result_pairs_summary(morphs, &summary) == CG_OK);
    assert(summary.unit == CG_PAIRS_MORPHEMES);
    assert(summary.pair_count == cg_result_pair_count(morphs));
    assert(summary.morpheme_count == 12 * 3 * 2 + 2 * 3);
    assert(summary.null_pairs > 0 && summary.strata > 0);
    assert(summary.morpheme_threshold > 0.0);
    assert(summary.level_counts[0] + summary.level_counts[1] + summary.level_counts[2] + summary.level_counts[3] == summary.pair_count);

    assert(cg_result_pairs_copy(morphs, &copied, &copied_count) == CG_OK);
    assert(copied_count == cg_result_pair_count(morphs));
    assert(strcmp(copied[0].form_a, cg_result_pair_at(morphs, 0)->form_a) == 0);
    assert(copied[0].form_a != cg_result_pair_at(morphs, 0)->form_a);
    assert(copied[5].weight == cg_result_pair_at(morphs, 5)->weight);
    cg_pair_rows_free(copied, copied_count);
    cg_pair_rows_free(NULL, 0);

    assert(cg_result_pairs_tsv(morphs, &text) == CG_OK);
    assert(strstr(text, "form_a\tmorph_a\tform_b\tmorph_b\tweight\tdistance\t") == text);
    assert(strstr(text, "\na_pebble\t0\tb_pebble\t0\t") != NULL);
    cg_string_free(text);
    assert(cg_result_pairs_json(morphs, &text) == CG_OK);
    assert(strstr(text, "\"role\": \"q\"") != NULL);
    assert(strstr(text, "\"producer\": \"cognator@0.1.0+merkmal.") != NULL);
    assert(strstr(text, "\"form_obs_a\": \"a_pebble#0\"") != NULL);
    assert(strstr(text, "\"diagnostics\"") != NULL);
    cg_string_free(text);
    assert(cg_result_pairs_tsv(words, &text) == CG_OK);
    assert(strstr(text, "form_a\tform_b\tweight\tdistance\t") == text);
    cg_string_free(text);
    text = NULL;

    /* Deterministic: the same spec gives the same document. */
    assert(cg_detect_with_pairs(ctx, records, &options, &spec, &again) == CG_OK);
    assert(cg_result_pairs_tsv(morphs, &text) == CG_OK);
    assert(cg_result_pairs_tsv(again, &text2) == CG_OK);
    assert(strcmp(text, text2) == 0);
    cg_string_free(text);
    cg_string_free(text2);
    text = NULL;
    text2 = NULL;
    assert(cg_write_pairs("test_pairs_out.json", again, 1) == CG_OK);
    assert(cg_write_sidecar_morphemes("test_pairs_sidecar.tsv", again) == CG_OK);
    assert(cg_write_sidecar_morphemes("test_pairs_sidecar.tsv", plain) == CG_ERR_INVALID_ARGUMENT);
    cg_result_free(again);
    again = NULL;

    spec.unit = 7;
    assert(cg_detect_with_pairs(ctx, records, &options, &spec, &again) == CG_ERR_UNSUPPORTED_OPTION);
    assert(again == NULL);

    cg_result_free(plain);
    cg_result_free(words);
    cg_result_free(morphs);
    cg_records_free(records);
    plain = words = morphs = NULL;
    records = NULL;

    /* No boundaries anywhere: the morpheme export is the word export. */
    write_text(
        "pairs_plain_fixture.tsv",
        "ID\tLanguage_ID\tConcept\tSegments\tFamily\n"
        "a1\tA\tC1\tp a k a\tf\n" "b1\tB\tC1\tp a g a\tf\n" "c1\tC\tC1\tb a k a\tf\n"
        "a2\tA\tC2\tt u k i\tf\n" "b2\tB\tC2\tt u g i\tf\n" "c2\tC\tC2\td u k i\tf\n"
        "a3\tA\tC3\tm e l o\tf\n" "b3\tB\tC3\tm e l o\tf\n" "c3\tC\tC3\tn a s u\tf\n"
    );
    assert(cg_read_table("pairs_plain_fixture.tsv", &records) == CG_OK);
    cg_pairs_spec_init_defaults(&spec);
    spec.min_null = 3;
    assert(cg_detect_with_pairs(ctx, records, &options, &spec, &words) == CG_OK);
    spec.unit = CG_PAIRS_MORPHEMES;
    assert(cg_detect_with_pairs(ctx, records, &options, &spec, &morphs) == CG_OK);
    assert(cg_result_pair_count(words) == cg_result_pair_count(morphs));
    for (i = 0; i < cg_result_pair_count(words); i++) {
        p = cg_result_pair_at(words, i);
        q = cg_result_pair_at(morphs, i);
        assert(strcmp(p->form_a, q->form_a) == 0 && strcmp(p->form_b, q->form_b) == 0);
        assert(q->morph_a == 0 && q->morph_b == 0);
        assert(p->weight == q->weight && p->distance == q->distance && p->null_count == q->null_count);
    }
    cg_result_free(words);
    cg_result_free(morphs);
    words = morphs = NULL;

    /* The exported distance is the one clustering uses. With the merkmal
     * prior (no posterior scorer) that is the public cg_align_distance_*,
     * which this can recompute directly, in both alignment modes. */
    {
        const char *modes[2] = {"combined", "global"};
        int m;
        for (m = 0; m < 2; m++) {
            cg_options prior_options;
            cg_options_init_defaults(&prior_options);
            prior_options.scorer_mode = "prior";
            prior_options.class_reduce = 0;
            prior_options.alignment_mode = modes[m];
            cg_pairs_spec_init_defaults(&spec);
            assert(cg_detect_with_pairs(ctx, records, &prior_options, &spec, &words) == CG_OK);
            assert(cg_result_pair_count(words) == 9);
            for (i = 0; i < cg_result_pair_count(words); i++) {
                char buf_a[64];
                char buf_b[64];
                const char *items_a[16];
                const char *items_b[16];
                uint64_t na;
                uint64_t nb;
                double expected = -1.0;
                p = cg_result_pair_at(words, i);
                na = split_segments(record_by_id(records, p->form_a)->segments, buf_a, sizeof(buf_a), items_a);
                nb = split_segments(record_by_id(records, p->form_b)->segments, buf_b, sizeof(buf_b), items_b);
                if (m == 0) {
                    assert(cg_align_distance_combined(ctx, items_a, na, items_b, nb, 1, &expected) == CG_OK);
                } else {
                    assert(cg_align_distance(ctx, items_a, na, items_b, nb, 1, &expected) == CG_OK);
                    assert(p->distance == p->global_distance);
                }
                assert(p->distance == expected);
            }
            cg_result_free(words);
            words = NULL;
        }
    }
    cg_records_free(records);

    /* One concept: there is no cross-concept background, so nothing is
     * claimed. */
    write_text(
        "pairs_one_concept.tsv",
        "ID\tLanguage_ID\tConcept\tSegments\tFamily\n"
        "a1\tA\tC1\tp a k a\tf\n" "b1\tB\tC1\tp a k a\tf\n"
    );
    assert(cg_read_table("pairs_one_concept.tsv", &records) == CG_OK);
    cg_pairs_spec_init_defaults(&spec);
    assert(cg_detect_with_pairs(ctx, records, &options, &spec, &words) == CG_OK);
    assert(cg_result_pair_count(words) == 1);
    p = cg_result_pair_at(words, 0);
    assert(p->null_level == CG_NULL_NONE && p->null_count == 0 && p->weight == 0.0);
    assert(p->distance == 0.0);
    cg_result_free(words);
    cg_records_free(records);

    assert(cg_result_pair_at(NULL, 0) == NULL);
    assert(cg_result_pair_count(NULL) == 0);
    assert(cg_result_pairs_copy(NULL, &copied, &copied_count) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_write_pairs("x.tsv", NULL, 0) == CG_ERR_INVALID_ARGUMENT);
    cg_context_free(ctx);
    return 0;
}
