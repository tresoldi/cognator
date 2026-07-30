#include "cognator.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static int approx(double a, double b, double tol) {
    double d = a - b;
    if (d < 0) {
        d = -d;
    }
    return d <= tol;
}

int main(void) {
    cg_context *ctx = NULL;
    cg_dirichlet_prior *prior = NULL;
    cg_posterior_table *table = NULL;
    cg_posterior_table *trained = NULL;
    char **inventory = NULL;
    uint64_t inventory_count = 0;
    double sum = 0.0;
    double alpha = 0.0;
    double mean = 0.0;
    double mean2 = 0.0;
    double flat = 0.0;
    double boosted = 0.0;
    double lo = 0.0;
    double other = 0.0;
    double score = 0.0;
    uint64_t i;
    const char *inv[] = {"a", "b", "e", "i", "o", "u", "k", "p", "t", "-"};
    const char *src1[] = {"p", "a"};
    const char *tgt1[] = {"b", "a"};
    const char *src2[] = {"p", "e"};
    const char *tgt2[] = {"b", "e"};
    cg_form_pair pairs[2];

    assert(cg_context_new_builtin(&ctx) == CG_OK);
    assert(cg_dirichlet_prior_new(ctx, 5.0, 4.0, inv, 10, &prior) == CG_OK);
    assert(cg_dirichlet_prior_inventory_count(prior) == 10);

    for (i = 0; i < cg_dirichlet_prior_inventory_count(prior); i++) {
        assert(cg_dirichlet_prior_alpha(prior, "p", cg_dirichlet_prior_inventory_at(prior, i), &alpha) == CG_OK);
        sum += alpha;
    }
    assert(approx(sum, 5.0, 1e-9));
    assert(cg_dirichlet_prior_alpha(prior, "p", "z", &alpha) == CG_OK);
    assert(alpha == 0.0);
    assert(cg_dirichlet_prior_alpha(prior, "p", "b", &alpha) == CG_OK);
    assert(cg_dirichlet_prior_alpha(prior, "p", "a", &mean) == CG_OK);
    assert(alpha > mean);

    assert(cg_posterior_table_new("S", "T", prior, &table) == CG_OK);
    assert(cg_posterior_mean(table, "a", "e", &mean) == CG_OK);
    assert(cg_dirichlet_prior_alpha(prior, "a", "e", &alpha) == CG_OK);
    assert(approx(mean, alpha / 5.0, 1e-9));
    for (i = 0; i < 1000; i++) {
        assert(cg_posterior_table_add(table, "p", "b") == CG_OK);
    }
    assert(cg_posterior_mean(table, "p", "b", &mean) == CG_OK);
    assert(mean > 0.99);
    cg_posterior_table_free(table);

    assert(cg_posterior_table_new("S", "T", prior, &table) == CG_OK);
    for (i = 0; i < 50; i++) {
        assert(cg_posterior_table_add(table, "a", "e") == CG_OK);
    }
    assert(cg_posterior_table_add(table, "a", "a") == CG_OK);
    assert(cg_posterior_table_add(table, "a", "i") == CG_OK);
    assert(cg_posterior_table_add(table, "a", "o") == CG_OK);
    assert(cg_posterior_table_add(table, "a", "u") == CG_OK);
    assert(cg_posterior_table_add(table, "e", "a") == CG_OK);
    assert(cg_posterior_table_add(table, "e", "i") == CG_OK);
    assert(cg_posterior_table_add(table, "e", "o") == CG_OK);
    assert(cg_posterior_table_add(table, "e", "u") == CG_OK);
    assert(cg_posterior_table_add(table, "i", "a") == CG_OK);
    assert(cg_posterior_table_add(table, "i", "i") == CG_OK);
    assert(cg_posterior_table_add(table, "i", "o") == CG_OK);
    assert(cg_posterior_table_add(table, "i", "u") == CG_OK);
    assert(cg_posterior_log_odds(table, "a", "e", &lo) == CG_OK);
    assert(cg_posterior_log_odds(table, "a", "i", &other) == CG_OK);
    assert(lo > 0.0);
    assert(lo > other);
    assert(cg_posterior_score(ctx, table, "a", "e", 1, &score) == CG_OK);
    assert(score >= -1.0 && score <= 1.0);
    assert(score > 0.0);
    cg_posterior_table_free(table);

    assert(cg_posterior_table_new("S", "T", prior, &table) == CG_OK);
    for (i = 0; i < 100; i++) {
        assert(cg_posterior_table_add(table, "a", "e") == CG_OK);
    }
    assert(cg_posterior_table_add(table, "i", "o") == CG_OK);
    for (i = 0; i < 10; i++) {
        assert(cg_posterior_table_add(table, "e", "i") == CG_OK);
    }
    assert(cg_posterior_mean(table, "i", "o", &flat) == CG_OK);
    assert(cg_posterior_table_attach_scaling(table, "sparse-boost") == CG_OK);
    assert(cg_posterior_mean(table, "i", "o", &boosted) == CG_OK);
    assert(boosted < flat);
    cg_posterior_table_free(table);

    assert(cg_posterior_table_new("S", "T", prior, &table) == CG_OK);
    for (i = 0; i < 100; i++) {
        assert(cg_posterior_table_add(table, "a", "e") == CG_OK);
    }
    for (i = 0; i < 5; i++) {
        assert(cg_posterior_table_add(table, "e", "i") == CG_OK);
        assert(cg_posterior_table_add(table, "i", "u") == CG_OK);
    }
    assert(cg_posterior_mean(table, "a", "e", &flat) == CG_OK);
    assert(cg_posterior_table_attach_scaling(table, "rich-boost") == CG_OK);
    assert(cg_posterior_mean(table, "a", "e", &boosted) == CG_OK);
    assert(boosted < flat);
    assert(cg_posterior_table_attach_scaling(table, "missing") == CG_ERR_UNSUPPORTED_OPTION);
    cg_posterior_table_free(table);

    memset(pairs, 0, sizeof(pairs));
    pairs[0].source = src1;
    pairs[0].source_count = 2;
    pairs[0].target = tgt1;
    pairs[0].target_count = 2;
    pairs[0].concept = "C1";
    pairs[1].source = src2;
    pairs[1].source_count = 2;
    pairs[1].target = tgt2;
    pairs[1].target_count = 2;
    pairs[1].concept = "C2";
    assert(cg_inventory_from_pairs(pairs, 2, &inventory, &inventory_count) == CG_OK);
    assert(inventory_count >= 5);
    assert(strcmp(inventory[0], "-") == 0);
    cg_string_array_free(inventory, inventory_count);

    assert(cg_train_posterior(ctx, "A", "B", pairs, 2, prior, 2, &trained) == CG_OK);
    assert(cg_posterior_table_total_pairs(trained) == 2);
    assert(cg_posterior_table_count_pair(trained, "p", "b") >= 1);
    assert(cg_posterior_table_count_pair(trained, "a", "a") >= 1);
    assert(cg_posterior_mean(trained, "p", "b", &mean) == CG_OK);
    assert(cg_posterior_mean(trained, "p", "a", &mean2) == CG_OK);
    assert(mean > mean2);
    cg_posterior_table_free(trained);

    assert(cg_dirichlet_prior_new(NULL, 5.0, 4.0, inv, 10, &prior) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_posterior_table_new(NULL, "T", prior, &table) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_posterior_table_attach_scaling(NULL, "") == CG_ERR_INVALID_ARGUMENT);
    assert(cg_posterior_mean(NULL, "a", "e", &mean) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_train_posterior(ctx, "A", "B", NULL, 1, prior, 1, &trained) == CG_ERR_INVALID_ARGUMENT);

    cg_dirichlet_prior_free(prior);
    cg_dirichlet_prior_free(NULL);
    cg_context_free(ctx);
    return 0;
}
