/* q(pairs): the pairwise export, at word or morpheme level.
 *
 * Every same-concept cross-lect pair of units (whole words, or the morphemes
 * between `+` boundaries) gets the distance the pipeline already computes,
 * with the per-lect-pair scorers trained on whole words, and a weight read
 * against an empirical null:
 *
 *   weight = 1 - share of null pairs whose distance is <= this pair's
 *
 * where the null is cross-CONCEPT pairs of units from the same lect pair whose
 * lengths fall in the same length bins (length_bin below), sampled boundedly
 * and deterministically. Short units match by chance more often, so a short
 * affix needs a smaller distance than a long root to earn the same weight.
 *
 * This is a background-match rate -- one minus a p-value-like quantity -- and
 * not a posterior probability of cognacy. docs/pairs.md says what it is and
 * is not; keep that file in step with the definitions here. */
#include "internal/data.h"
#include "internal/pairs.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CG_PAIRS_DEFAULT_NULL_SAMPLES 256
#define CG_PAIRS_DEFAULT_MIN_NULL 20
#define CG_PAIRS_DEFAULT_NULL_SEED 1u
/* The serialization of the q_pairs document this file writes. */
#define CG_PAIRS_SCHEMA_VERSION "0.1.0"
#define CG_PAIRS_ALL_BINS (-1)

typedef struct pairs_unit {
    size_t rec;
    size_t morph;
    size_t start;
    size_t len;
    size_t lect;
    size_t concept;
} pairs_unit;

typedef struct scored_pair {
    size_t ua;
    size_t ub;
    double distance;
    cg_internal_components comp;
    size_t lo;      /* lect index of the lower-sorting lect */
    size_t hi;
    int bin_lo;     /* length bin of the unit from lect lo */
    int bin_hi;
} scored_pair;

typedef struct null_stratum {
    size_t lo;
    size_t hi;
    int bin_lo;
    int bin_hi;
    double *d;      /* sorted ascending */
    size_t n;
} null_stratum;

typedef struct intern_table {
    char **names;
    size_t count;
    size_t *slots;  /* open addressing over indices + 1; 0 is empty */
    size_t slot_count;
} intern_table;

/* ---- small helpers ---------------------------------------------------- */

static uint64_t fnv1a(uint64_t h, const char *s) {
    const unsigned char *p = (const unsigned char *)(s == NULL ? "" : s);
    while (*p) {
        h ^= (uint64_t)*p++;
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* Length bins shared by words and morphemes, so a corpus without `+` gives
 * the same weights in either unit: exact up to 6 segments (where affixes
 * live), then 7-8, 9-10, 11-13, 14+. */
static int length_bin(size_t len) {
    if (len <= 6) return (int)len;
    if (len <= 8) return 7;
    if (len <= 10) return 9;
    if (len <= 13) return 11;
    return 14;
}

static void intern_free(intern_table *t) {
    size_t i;
    for (i = 0; i < t->count; i++) free(t->names[i]);
    free(t->names);
    free(t->slots);
    *t = (intern_table){0};
}

static cg_status intern(intern_table *t, const char *name, size_t *out) {
    uint64_t h;
    size_t slot;
    const char *key = name == NULL ? "" : name;
    if (t->slot_count == 0 || (t->count + 1) * 2 > t->slot_count) {
        size_t next_count = t->slot_count == 0 ? 64 : t->slot_count * 2;
        size_t *next = (size_t *)calloc(next_count, sizeof(*next));
        size_t i;
        if (next == NULL) return CG_ERR_OOM;
        for (i = 0; i < t->count; i++) {
            size_t s = (size_t)(fnv1a(1469598103934665603ull, t->names[i]) % next_count);
            while (next[s] != 0) s = (s + 1) % next_count;
            next[s] = i + 1;
        }
        free(t->slots);
        t->slots = next;
        t->slot_count = next_count;
    }
    h = fnv1a(1469598103934665603ull, key);
    slot = (size_t)(h % t->slot_count);
    while (t->slots[slot] != 0) {
        size_t idx = t->slots[slot] - 1;
        if (strcmp(t->names[idx], key) == 0) {
            *out = idx;
            return CG_OK;
        }
        slot = (slot + 1) % t->slot_count;
    }
    {
        char **next = (char **)realloc(t->names, (t->count + 1) * sizeof(*next));
        if (next == NULL) return CG_ERR_OOM;
        t->names = next;
        t->names[t->count] = cg_strdup(key);
        if (t->names[t->count] == NULL) return CG_ERR_OOM;
    }
    t->slots[slot] = t->count + 1;
    *out = t->count++;
    return CG_OK;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a;
    double y = *(const double *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static int cmp_stratum(const void *a, const void *b) {
    const null_stratum *x = (const null_stratum *)a;
    const null_stratum *y = (const null_stratum *)b;
    if (x->lo != y->lo) return x->lo < y->lo ? -1 : 1;
    if (x->hi != y->hi) return x->hi < y->hi ? -1 : 1;
    if (x->bin_lo != y->bin_lo) return x->bin_lo < y->bin_lo ? -1 : 1;
    if (x->bin_hi != y->bin_hi) return x->bin_hi < y->bin_hi ? -1 : 1;
    return 0;
}

static null_stratum *find_stratum(null_stratum *strata, size_t count, size_t lo, size_t hi, int bin_lo, int bin_hi) {
    null_stratum key;
    memset(&key, 0, sizeof(key));
    key.lo = lo;
    key.hi = hi;
    key.bin_lo = bin_lo;
    key.bin_hi = bin_hi;
    return (null_stratum *)bsearch(&key, strata, count, sizeof(*strata), cmp_stratum);
}

/* Number of sorted values <= x. */
static size_t count_le(const double *sorted, size_t n, double x) {
    size_t lo = 0;
    size_t hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (sorted[mid] <= x) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

void cg_internal_pairs_spec_effective(cg_pairs_spec *dst, const cg_pairs_spec *src) {
    cg_pairs_spec_init_defaults(dst);
    if (src != NULL) *dst = *src;
    if (dst->null_samples <= 0) dst->null_samples = CG_PAIRS_DEFAULT_NULL_SAMPLES;
    if (dst->min_null <= 0) dst->min_null = CG_PAIRS_DEFAULT_MIN_NULL;
    if (dst->null_seed == 0) dst->null_seed = CG_PAIRS_DEFAULT_NULL_SEED;
}

void cg_pairs_spec_init_defaults(cg_pairs_spec *spec) {
    if (spec == NULL) return;
    memset(spec, 0, sizeof(*spec));
    spec->unit = CG_PAIRS_WORDS;
    spec->null_samples = CG_PAIRS_DEFAULT_NULL_SAMPLES;
    spec->min_null = CG_PAIRS_DEFAULT_MIN_NULL;
    spec->null_seed = CG_PAIRS_DEFAULT_NULL_SEED;
    spec->morpheme_clusters = 0;
}

/* ---- units -------------------------------------------------------------- */

/* The morphemes of a row: [0, b1), [b1, b2), ... [bk, n) over its `+` offsets,
 * with empty spans (a leading, trailing or doubled `+`) dropped -- the rule
 * regulae's loaders apply to `breaks`, so morpheme k here is morpheme k there.
 * A row without `+`, or whose spans are all empty, is one unit: the word. */
static cg_status push_units(pairs_unit **units, size_t *count, size_t *cap, const pairs_unit *base, const cg_pairs_form *form, int morphemes) {
    size_t starts[1 + 64];
    size_t ends[1 + 64];
    size_t spans = 0;
    size_t k;
    if (morphemes && form->break_count > 0) {
        size_t prev = 0;
        size_t limit = form->break_count > 64 ? 64 : form->break_count;
        for (k = 0; k <= limit; k++) {
            size_t end = k < limit ? form->breaks[k] : form->count;
            if (end > form->count) end = form->count;
            if (end > prev) {
                starts[spans] = prev;
                ends[spans] = end;
                spans++;
            }
            if (end > prev) prev = end;
        }
    }
    if (spans == 0) {
        starts[0] = 0;
        ends[0] = form->count;
        spans = 1;
    }
    for (k = 0; k < spans; k++) {
        if (*count == *cap) {
            size_t next_cap = *cap == 0 ? 64 : *cap * 2;
            pairs_unit *next = (pairs_unit *)realloc(*units, next_cap * sizeof(*next));
            if (next == NULL) return CG_ERR_OOM;
            *units = next;
            *cap = next_cap;
        }
        (*units)[*count] = *base;
        (*units)[*count].morph = k;
        (*units)[*count].start = starts[k];
        (*units)[*count].len = ends[k] - starts[k];
        (*count)++;
    }
    return CG_OK;
}

/* ---- the null ------------------------------------------------------------ */

/* Draws the null sample of one stratum: cross-concept pairs (x, y) with x from
 * lect lo and y from lect hi, x's length in bin_lo and y's in bin_hi (or any
 * length for CG_PAIRS_ALL_BINS). Every pair is taken when there are at most
 * null_samples candidates; otherwise null_samples are drawn uniformly with
 * replacement, rejecting same-concept draws, from a generator seeded by the
 * spec's seed and the stratum's own key (lect names and bins) -- so a
 * stratum's sample does not depend on which other strata were drawn first. */
static cg_status draw_stratum(const cg_pairs_input *in, const pairs_unit *units, size_t *const *lect_units, const size_t *lect_unit_counts, const intern_table *lects, null_stratum *s, uint64_t *distance_calls) {
    size_t *xs = NULL;
    size_t *ys = NULL;
    size_t nx = 0;
    size_t ny = 0;
    size_t i;
    size_t cap = (size_t)in->spec.null_samples;
    cg_status status = CG_OK;

    s->d = NULL;
    s->n = 0;
    xs = (size_t *)malloc((lect_unit_counts[s->lo] + 1) * sizeof(*xs));
    ys = (size_t *)malloc((lect_unit_counts[s->hi] + 1) * sizeof(*ys));
    s->d = (double *)malloc((cap + 1) * sizeof(*s->d));
    if (xs == NULL || ys == NULL || s->d == NULL) {
        status = CG_ERR_OOM;
        goto done;
    }
    for (i = 0; i < lect_unit_counts[s->lo]; i++) {
        size_t u = lect_units[s->lo][i];
        if (s->bin_lo == CG_PAIRS_ALL_BINS || length_bin(units[u].len) == s->bin_lo) xs[nx++] = u;
    }
    for (i = 0; i < lect_unit_counts[s->hi]; i++) {
        size_t u = lect_units[s->hi][i];
        if (s->bin_hi == CG_PAIRS_ALL_BINS || length_bin(units[u].len) == s->bin_hi) ys[ny++] = u;
    }
    if (nx == 0 || ny == 0) goto done;
    if ((uint64_t)nx * (uint64_t)ny <= (uint64_t)cap) {
        size_t a;
        size_t b;
        for (a = 0; a < nx && status == CG_OK; a++) {
            for (b = 0; b < ny; b++) {
                const pairs_unit *x = &units[xs[a]];
                const pairs_unit *y = &units[ys[b]];
                const cg_pairs_form *fx = &in->forms[x->rec];
                const cg_pairs_form *fy = &in->forms[y->rec];
                cg_internal_components comp;
                double d = 1.0;
                if (x->concept == y->concept) continue;
                status = in->distance(in->user, x->rec, fx->items + x->start, x->len, y->rec, fy->items + y->start, y->len, &comp, &d);
                if (status != CG_OK) break;
                (*distance_calls)++;
                s->d[s->n++] = d;
            }
        }
    } else {
        uint64_t state = in->spec.null_seed;
        uint64_t total = (uint64_t)nx * (uint64_t)ny;
        size_t attempts = 0;
        size_t max_attempts = cap * 8;
        state ^= fnv1a(fnv1a(1469598103934665603ull, lects->names[s->lo]), lects->names[s->hi]);
        state ^= (uint64_t)(s->bin_lo + 2) * UINT64_C(0xD6E8FEB86659FD93);
        state ^= (uint64_t)(s->bin_hi + 2) * UINT64_C(0xA0761D6478BD642F);
        while (s->n < cap && attempts < max_attempts) {
            uint64_t r = splitmix64(&state) % total;
            const pairs_unit *x = &units[xs[(size_t)(r / ny)]];
            const pairs_unit *y = &units[ys[(size_t)(r % ny)]];
            const cg_pairs_form *fx = &in->forms[x->rec];
            const cg_pairs_form *fy = &in->forms[y->rec];
            cg_internal_components comp;
            double d = 1.0;
            attempts++;
            if (x->concept == y->concept) continue;
            status = in->distance(in->user, x->rec, fx->items + x->start, x->len, y->rec, fy->items + y->start, y->len, &comp, &d);
            if (status != CG_OK) break;
            (*distance_calls)++;
            s->d[s->n++] = d;
        }
    }
    if (status == CG_OK && s->n > 1) qsort(s->d, s->n, sizeof(*s->d), cmp_double);
done:
    free(xs);
    free(ys);
    if (status != CG_OK) {
        free(s->d);
        s->d = NULL;
        s->n = 0;
    }
    return status;
}

/* ---- morpheme clustering ------------------------------------------------- */

static cg_status make_morpheme_id(char **out, const char *family, const char *concept, uint64_t module) {
    size_t len = strlen(family ? family : "") + strlen(concept ? concept : "") + 64;
    char *s = (char *)malloc(len);
    if (s == NULL) return CG_ERR_OOM;
    snprintf(s, len, "%s:%s:m%llu", family ? family : "", concept ? concept : "", (unsigned long long)module);
    *out = s;
    return CG_OK;
}

/* The existing graph policy and Infomap, run per concept on morpheme nodes
 * with the morpheme distances. A morpheme whose module holds no unit of
 * another lect is in no cross-lect set and gets '-'. */
static cg_status cluster_morphemes(const cg_pairs_input *in, const pairs_unit *units, size_t unit_count, const scored_pair *pairs, size_t pair_count, size_t concept_count, const size_t *first_unit, const size_t *unit_counts, cg_result *result) {
    const cg_records *records = in->records;
    double *all_d = NULL;
    double threshold;
    size_t *concept_unit_count = NULL;
    size_t *concept_unit_start = NULL;
    size_t *concept_units = NULL;
    size_t *concept_pair_count = NULL;
    size_t *concept_pair_start = NULL;
    size_t *concept_pairs = NULL;
    size_t *local = NULL;
    char **unit_ids = NULL;
    size_t i;
    size_t c;
    cg_status status = CG_OK;

    all_d = (double *)malloc((pair_count + 1) * sizeof(*all_d));
    concept_unit_count = (size_t *)calloc(concept_count + 1, sizeof(size_t));
    concept_unit_start = (size_t *)calloc(concept_count + 1, sizeof(size_t));
    concept_units = (size_t *)malloc((unit_count + 1) * sizeof(size_t));
    concept_pair_count = (size_t *)calloc(concept_count + 1, sizeof(size_t));
    concept_pair_start = (size_t *)calloc(concept_count + 1, sizeof(size_t));
    concept_pairs = (size_t *)malloc((pair_count + 1) * sizeof(size_t));
    local = (size_t *)malloc((unit_count + 1) * sizeof(size_t));
    unit_ids = (char **)calloc(unit_count + 1, sizeof(char *));
    if (all_d == NULL || concept_unit_count == NULL || concept_unit_start == NULL || concept_units == NULL ||
        concept_pair_count == NULL || concept_pair_start == NULL || concept_pairs == NULL || local == NULL || unit_ids == NULL) {
        status = CG_ERR_OOM;
        goto done;
    }
    for (i = 0; i < pair_count; i++) all_d[i] = pairs[i].distance;
    if (in->fixed_threshold > 0) {
        threshold = in->fixed_threshold;
    } else {
        threshold = cg_clamp(cg_otsu_threshold(all_d, (uint64_t)pair_count, 0.0, 1.0, 64), in->threshold_min, in->threshold_max);
    }
    result->pairs_summary.morpheme_threshold = threshold;

    /* Bucket units and pairs by concept, keeping input order. */
    for (i = 0; i < unit_count; i++) concept_unit_count[units[i].concept]++;
    for (c = 1; c < concept_count; c++) concept_unit_start[c] = concept_unit_start[c - 1] + concept_unit_count[c - 1];
    memset(concept_unit_count, 0, (concept_count + 1) * sizeof(size_t));
    for (i = 0; i < unit_count; i++) {
        size_t cc = units[i].concept;
        local[i] = concept_unit_count[cc];
        concept_units[concept_unit_start[cc] + concept_unit_count[cc]++] = i;
    }
    for (i = 0; i < pair_count; i++) concept_pair_count[units[pairs[i].ua].concept]++;
    for (c = 1; c < concept_count; c++) concept_pair_start[c] = concept_pair_start[c - 1] + concept_pair_count[c - 1];
    memset(concept_pair_count, 0, (concept_count + 1) * sizeof(size_t));
    for (i = 0; i < pair_count; i++) {
        size_t cc = units[pairs[i].ua].concept;
        concept_pairs[concept_pair_start[cc] + concept_pair_count[cc]++] = i;
    }

    for (c = 0; c < concept_count && status == CG_OK; c++) {
        size_t n = concept_unit_count[c];
        size_t np = concept_pair_count[c];
        cg_distance_pair *dp = NULL;
        cg_edge *edges = NULL;
        uint64_t edge_count = 0;
        cg_graph *graph = NULL;
        cg_partition *part = NULL;
        cg_partition *refined = NULL;
        size_t *module_first_lect = NULL;
        unsigned char *module_multi = NULL;
        size_t k;
        if (n == 0) continue;
        dp = (cg_distance_pair *)calloc(np + 1, sizeof(*dp));
        if (dp == NULL) { status = CG_ERR_OOM; goto concept_done; }
        for (k = 0; k < np; k++) {
            const scored_pair *p = &pairs[concept_pairs[concept_pair_start[c] + k]];
            dp[k] = (cg_distance_pair){(uint64_t)local[p->ua], (uint64_t)local[p->ub], p->distance};
        }
        status = cg_build_graph_edges((uint64_t)n, dp, (uint64_t)np, threshold, &in->graph_spec, &edges, &edge_count);
        if (status == CG_OK) status = cg_graph_new((uint64_t)n, edges, edge_count, &graph);
        if (status == CG_OK) status = cg_cluster_infomap(graph, in->seed, &part);
        if (status == CG_OK) status = cg_refine_partition(graph, part, &in->refine_spec, &refined);
        if (status != CG_OK) goto concept_done;
        module_first_lect = (size_t *)malloc((n + 1) * sizeof(size_t));
        module_multi = (unsigned char *)calloc(n + 1, 1);
        if (module_first_lect == NULL || module_multi == NULL) { status = CG_ERR_OOM; goto concept_done; }
        for (k = 0; k <= n; k++) module_first_lect[k] = SIZE_MAX;
        for (k = 0; k < n; k++) {
            uint64_t m = cg_partition_module_at(refined, (uint64_t)k);
            size_t lect = units[concept_units[concept_unit_start[c] + k]].lect;
            if (m > n) continue;
            if (module_first_lect[m] == SIZE_MAX) module_first_lect[m] = lect;
            else if (module_first_lect[m] != lect) module_multi[m] = 1;
        }
        for (k = 0; k < n; k++) {
            size_t u = concept_units[concept_unit_start[c] + k];
            const cg_record *r = &records->items[units[u].rec];
            uint64_t m = cg_partition_module_at(refined, (uint64_t)k);
            if (m <= n && module_multi[m]) {
                status = make_morpheme_id(&unit_ids[u], r->family, r->concept, m);
            } else {
                unit_ids[u] = cg_strdup("-");
                if (unit_ids[u] == NULL) status = CG_ERR_OOM;
            }
            if (status != CG_OK) break;
        }
concept_done:
        free(dp);
        cg_edges_free(edges);
        cg_graph_free(graph);
        cg_partition_free(part);
        cg_partition_free(refined);
        free(module_first_lect);
        free(module_multi);
    }
    if (status != CG_OK) goto done;

    result->morpheme_ids = (char **)calloc(records->count + 1, sizeof(char *));
    if (result->morpheme_ids == NULL) { status = CG_ERR_OOM; goto done; }
    for (i = 0; i < records->count; i++) {
        size_t len = 1;
        size_t k;
        char *s;
        for (k = 0; k < unit_counts[i]; k++) len += strlen(unit_ids[first_unit[i] + k]) + 1;
        s = (char *)malloc(len);
        if (s == NULL) { status = CG_ERR_OOM; goto done; }
        s[0] = '\0';
        for (k = 0; k < unit_counts[i]; k++) {
            if (k > 0) strcat(s, " ");
            strcat(s, unit_ids[first_unit[i] + k]);
        }
        result->morpheme_ids[i] = s;
    }
done:
    if (unit_ids != NULL) {
        for (i = 0; i < unit_count; i++) free(unit_ids[i]);
    }
    free(unit_ids);
    free(all_d);
    free(concept_unit_count);
    free(concept_unit_start);
    free(concept_units);
    free(concept_pair_count);
    free(concept_pair_start);
    free(concept_pairs);
    free(local);
    return status;
}

/* ---- the run ------------------------------------------------------------- */

static cg_status push_scored(scored_pair **pairs, size_t *count, size_t *cap, const scored_pair *p) {
    if (*count == *cap) {
        size_t next_cap = *cap == 0 ? 64 : *cap * 2;
        scored_pair *next = (scored_pair *)realloc(*pairs, next_cap * sizeof(*next));
        if (next == NULL) return CG_ERR_OOM;
        *pairs = next;
        *cap = next_cap;
    }
    (*pairs)[(*count)++] = *p;
    return CG_OK;
}

cg_status cg_internal_pairs_run(const cg_pairs_input *in, cg_result *result) {
    const cg_records *records = in->records;
    int morphemes = in->spec.unit == CG_PAIRS_MORPHEMES;
    intern_table lects = {0};
    intern_table concepts = {0};
    size_t *rec_lect = NULL;
    size_t *rec_concept = NULL;
    pairs_unit *units = NULL;
    size_t unit_count = 0;
    size_t unit_cap = 0;
    size_t *first_unit = NULL;
    size_t *unit_counts = NULL;
    size_t **concept_recs = NULL;
    size_t *concept_rec_counts = NULL;
    size_t **lect_units = NULL;
    size_t *lect_unit_counts = NULL;
    scored_pair *pairs = NULL;
    size_t pair_count = 0;
    size_t pair_cap = 0;
    null_stratum *strata = NULL;
    size_t stratum_count = 0;
    null_stratum *pools = NULL;
    size_t pool_count = 0;
    uint64_t distance_calls = 0;
    size_t i;
    cg_status status = CG_OK;

    memset(&result->pairs_summary, 0, sizeof(result->pairs_summary));
    rec_lect = (size_t *)malloc((records->count + 1) * sizeof(size_t));
    rec_concept = (size_t *)malloc((records->count + 1) * sizeof(size_t));
    first_unit = (size_t *)malloc((records->count + 1) * sizeof(size_t));
    unit_counts = (size_t *)malloc((records->count + 1) * sizeof(size_t));
    if (rec_lect == NULL || rec_concept == NULL || first_unit == NULL || unit_counts == NULL) {
        status = CG_ERR_OOM;
        goto done;
    }

    /* Units, and the lect / concept of each. */
    for (i = 0; i < records->count; i++) {
        const cg_record *r = &records->items[i];
        pairs_unit base;
        status = intern(&lects, r->language_id, &rec_lect[i]);
        if (status == CG_OK) status = intern(&concepts, r->concept, &rec_concept[i]);
        if (status != CG_OK) goto done;
        memset(&base, 0, sizeof(base));
        base.rec = i;
        base.lect = rec_lect[i];
        base.concept = rec_concept[i];
        first_unit[i] = unit_count;
        status = push_units(&units, &unit_count, &unit_cap, &base, &in->forms[i], morphemes);
        if (status != CG_OK) goto done;
        unit_counts[i] = unit_count - first_unit[i];
    }
    concept_recs = (size_t **)calloc(concepts.count + 1, sizeof(size_t *));
    concept_rec_counts = (size_t *)calloc(concepts.count + 1, sizeof(size_t));
    lect_units = (size_t **)calloc(lects.count + 1, sizeof(size_t *));
    lect_unit_counts = (size_t *)calloc(lects.count + 1, sizeof(size_t));
    if (concept_recs == NULL || concept_rec_counts == NULL || lect_units == NULL || lect_unit_counts == NULL) {
        status = CG_ERR_OOM;
        goto done;
    }
    for (i = 0; i < records->count; i++) concept_rec_counts[rec_concept[i]]++;
    for (i = 0; i < unit_count; i++) lect_unit_counts[units[i].lect]++;
    for (i = 0; i < concepts.count; i++) {
        concept_recs[i] = (size_t *)malloc((concept_rec_counts[i] + 1) * sizeof(size_t));
        if (concept_recs[i] == NULL) { status = CG_ERR_OOM; goto done; }
        concept_rec_counts[i] = 0;
    }
    for (i = 0; i < lects.count; i++) {
        lect_units[i] = (size_t *)malloc((lect_unit_counts[i] + 1) * sizeof(size_t));
        if (lect_units[i] == NULL) { status = CG_ERR_OOM; goto done; }
        lect_unit_counts[i] = 0;
    }
    for (i = 0; i < records->count; i++) {
        size_t c = rec_concept[i];
        concept_recs[c][concept_rec_counts[c]++] = i;
    }
    for (i = 0; i < unit_count; i++) {
        size_t l = units[i].lect;
        lect_units[l][lect_unit_counts[l]++] = i;
    }

    /* Scored pairs: rows i < j of one concept and different lects, then every
     * morpheme of i against every morpheme of j. Two morphemes of one word
     * share its lect, so they are never paired. */
    for (i = 0; i < records->count; i++) {
        size_t c = rec_concept[i];
        size_t k;
        for (k = 0; k < concept_rec_counts[c]; k++) {
            size_t j = concept_recs[c][k];
            size_t ma;
            size_t mb;
            int i_is_lo;
            if (j <= i || rec_lect[i] == rec_lect[j]) continue;
            i_is_lo = strcmp(lects.names[rec_lect[i]], lects.names[rec_lect[j]]) < 0;
            for (ma = 0; ma < unit_counts[i]; ma++) {
                for (mb = 0; mb < unit_counts[j]; mb++) {
                    scored_pair p;
                    const pairs_unit *ua = &units[first_unit[i] + ma];
                    const pairs_unit *ub = &units[first_unit[j] + mb];
                    memset(&p, 0, sizeof(p));
                    p.ua = first_unit[i] + ma;
                    p.ub = first_unit[j] + mb;
                    status = in->distance(in->user, i, in->forms[i].items + ua->start, ua->len, j, in->forms[j].items + ub->start, ub->len, &p.comp, &p.distance);
                    if (status != CG_OK) goto done;
                    p.lo = i_is_lo ? ua->lect : ub->lect;
                    p.hi = i_is_lo ? ub->lect : ua->lect;
                    p.bin_lo = length_bin(i_is_lo ? ua->len : ub->len);
                    p.bin_hi = length_bin(i_is_lo ? ub->len : ua->len);
                    status = push_scored(&pairs, &pair_count, &pair_cap, &p);
                    if (status != CG_OK) goto done;
                }
            }
        }
    }

    /* The strata the scored pairs need, each drawn once. */
    if (pair_count > 0) {
        strata = (null_stratum *)calloc(pair_count, sizeof(*strata));
        if (strata == NULL) { status = CG_ERR_OOM; goto done; }
        for (i = 0; i < pair_count; i++) {
            strata[i].lo = pairs[i].lo;
            strata[i].hi = pairs[i].hi;
            strata[i].bin_lo = pairs[i].bin_lo;
            strata[i].bin_hi = pairs[i].bin_hi;
        }
        qsort(strata, pair_count, sizeof(*strata), cmp_stratum);
        stratum_count = 0;
        for (i = 0; i < pair_count; i++) {
            if (stratum_count == 0 || cmp_stratum(&strata[stratum_count - 1], &strata[i]) != 0) {
                strata[stratum_count++] = strata[i];
            }
        }
        for (i = 0; i < stratum_count; i++) {
            status = draw_stratum(in, units, lect_units, lect_unit_counts, &lects, &strata[i], &distance_calls);
            if (status != CG_OK) goto done;
        }
        pools = (null_stratum *)calloc(stratum_count, sizeof(*pools));
        if (pools == NULL) { status = CG_ERR_OOM; goto done; }
    }

    /* Weights. */
    result->pairs = (cg_pair_row *)calloc(pair_count + 1, sizeof(*result->pairs));
    if (result->pairs == NULL) { status = CG_ERR_OOM; goto done; }
    for (i = 0; i < pair_count; i++) {
        const scored_pair *p = &pairs[i];
        const pairs_unit *ua = &units[p->ua];
        const pairs_unit *ub = &units[p->ub];
        null_stratum *s = find_stratum(strata, stratum_count, p->lo, p->hi, p->bin_lo, p->bin_hi);
        const null_stratum *use = s;
        int level = CG_NULL_STRATUM;
        cg_pair_row *row = &result->pairs[i];
        if (s->n < (size_t)in->spec.min_null) {
            null_stratum *pool = NULL;
            size_t k;
            for (k = 0; k < pool_count; k++) {
                if (pools[k].lo == p->lo && pools[k].hi == p->hi) {
                    pool = &pools[k];
                    break;
                }
            }
            if (pool == NULL) {
                pool = &pools[pool_count];
                memset(pool, 0, sizeof(*pool));
                pool->lo = p->lo;
                pool->hi = p->hi;
                pool->bin_lo = CG_PAIRS_ALL_BINS;
                pool->bin_hi = CG_PAIRS_ALL_BINS;
                status = draw_stratum(in, units, lect_units, lect_unit_counts, &lects, pool, &distance_calls);
                if (status != CG_OK) goto done;
                pool_count++;
            }
            if (pool->n >= (size_t)in->spec.min_null) {
                use = pool;
                level = CG_NULL_LECT_PAIR;
            } else if (pool->n > 0 || s->n > 0) {
                use = pool->n >= s->n ? pool : s;
                level = CG_NULL_SPARSE;
            } else {
                use = NULL;
                level = CG_NULL_NONE;
            }
        }
        row->form_a = cg_strdup(records->items[ua->rec].id);
        row->form_b = cg_strdup(records->items[ub->rec].id);
        if (row->form_a == NULL || row->form_b == NULL) {
            result->pair_count = i + 1;
            status = CG_ERR_OOM;
            goto done;
        }
        row->morph_a = (uint64_t)ua->morph;
        row->morph_b = (uint64_t)ub->morph;
        row->distance = p->distance;
        row->global_distance = p->comp.global;
        row->local_distance = p->comp.local;
        row->overlap_distance = p->comp.overlap;
        row->null_level = level;
        if (use == NULL || use->n == 0) {
            /* No background exists to beat, so nothing is claimed. */
            row->null_count = 0;
            row->weight = 0.0;
        } else {
            row->null_count = (uint64_t)use->n;
            row->weight = 1.0 - (double)count_le(use->d, use->n, p->distance) / (double)use->n;
        }
        result->pairs_summary.level_counts[level]++;
        result->pairs_summary.mean_weight += row->weight;
        if (row->weight >= 0.5) result->pairs_summary.weight_ge_050++;
        if (row->weight >= 0.95) result->pairs_summary.weight_ge_095++;
    }
    result->pair_count = pair_count;
    result->has_pairs = 1;
    result->pairs_summary.unit = morphemes ? CG_PAIRS_MORPHEMES : CG_PAIRS_WORDS;
    result->pairs_summary.pair_count = (uint64_t)pair_count;
    result->pairs_summary.strata = (uint64_t)(stratum_count + pool_count);
    result->pairs_summary.null_pairs = distance_calls;
    result->pairs_summary.null_samples = in->spec.null_samples;
    result->pairs_summary.min_null = in->spec.min_null;
    result->pairs_summary.null_seed = in->spec.null_seed;
    result->pairs_summary.threshold = in->word_threshold;
    result->pairs_summary.morpheme_count = (uint64_t)unit_count;
    if (pair_count > 0) result->pairs_summary.mean_weight /= (double)pair_count;

    if (morphemes && in->spec.morpheme_clusters) {
        status = cluster_morphemes(in, units, unit_count, pairs, pair_count, concepts.count, first_unit, unit_counts, result);
    }

done:
    for (i = 0; i < stratum_count; i++) free(strata[i].d);
    free(strata);
    for (i = 0; i < pool_count; i++) free(pools[i].d);
    free(pools);
    free(pairs);
    if (concept_recs != NULL) {
        for (i = 0; i < concepts.count; i++) free(concept_recs[i]);
    }
    free(concept_recs);
    free(concept_rec_counts);
    if (lect_units != NULL) {
        for (i = 0; i < lects.count; i++) free(lect_units[i]);
    }
    free(lect_units);
    free(lect_unit_counts);
    free(units);
    free(first_unit);
    free(unit_counts);
    free(rec_lect);
    free(rec_concept);
    intern_free(&lects);
    intern_free(&concepts);
    return status;
}

/* ---- accessors ----------------------------------------------------------- */

void cg_free_pair_row(cg_pair_row *row) {
    if (row == NULL) return;
    free((char *)row->form_a);
    free((char *)row->form_b);
    *row = (cg_pair_row){0};
}

uint64_t cg_result_pair_count(const cg_result *result) {
    return result == NULL ? 0 : (uint64_t)result->pair_count;
}

const cg_pair_row *cg_result_pair_at(const cg_result *result, uint64_t index) {
    if (result == NULL || index >= result->pair_count) return NULL;
    return &result->pairs[index];
}

cg_status cg_result_pairs_copy(const cg_result *result, cg_pair_row **rows_out, uint64_t *count_out) {
    cg_pair_row *rows;
    size_t i;
    if (result == NULL || rows_out == NULL || count_out == NULL) return CG_ERR_INVALID_ARGUMENT;
    *rows_out = NULL;
    *count_out = 0;
    rows = (cg_pair_row *)calloc(result->pair_count + 1, sizeof(*rows));
    if (rows == NULL) return CG_ERR_OOM;
    for (i = 0; i < result->pair_count; i++) {
        rows[i] = result->pairs[i];
        rows[i].form_a = cg_strdup(result->pairs[i].form_a);
        rows[i].form_b = cg_strdup(result->pairs[i].form_b);
        if (rows[i].form_a == NULL || rows[i].form_b == NULL) {
            cg_pair_rows_free(rows, (uint64_t)(i + 1));
            return CG_ERR_OOM;
        }
    }
    *rows_out = rows;
    *count_out = (uint64_t)result->pair_count;
    return CG_OK;
}

void cg_pair_rows_free(cg_pair_row *rows, uint64_t count) {
    uint64_t i;
    if (rows == NULL) return;
    for (i = 0; i < count; i++) cg_free_pair_row(&rows[i]);
    free(rows);
}

cg_status cg_result_pairs_summary(const cg_result *result, cg_pairs_summary *out) {
    if (result == NULL || out == NULL || !result->has_pairs) return CG_ERR_INVALID_ARGUMENT;
    *out = result->pairs_summary;
    return CG_OK;
}

const char *cg_result_morpheme_ids_at(const cg_result *result, uint64_t index) {
    if (result == NULL || result->morpheme_ids == NULL || index >= result->row_count) return NULL;
    return result->morpheme_ids[index];
}

/* ---- serialization ------------------------------------------------------- */

typedef struct sbuf {
    char *data;
    size_t len;
    size_t cap;
    int failed;
} sbuf;

static void sb_printf(sbuf *b, const char *fmt, ...) {
    va_list ap;
    int need;
    if (b->failed) return;
    va_start(ap, fmt);
    need = vsnprintf(b->data == NULL ? NULL : b->data + b->len, b->data == NULL ? 0 : b->cap - b->len, fmt, ap);
    va_end(ap);
    if (need < 0) {
        b->failed = 1;
        return;
    }
    if (b->data == NULL || b->len + (size_t)need + 1 > b->cap) {
        size_t next_cap = b->cap == 0 ? 4096 : b->cap;
        char *next;
        while (b->len + (size_t)need + 1 > next_cap) next_cap *= 2;
        next = (char *)realloc(b->data, next_cap);
        if (next == NULL) {
            b->failed = 1;
            return;
        }
        b->data = next;
        b->cap = next_cap;
        va_start(ap, fmt);
        vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
    }
    b->len += (size_t)need;
}

static void sb_json_string(sbuf *b, const char *s) {
    const unsigned char *p = (const unsigned char *)(s == NULL ? "" : s);
    sb_printf(b, "\"");
    for (; *p; p++) {
        if (*p == '"') sb_printf(b, "\\\"");
        else if (*p == '\\') sb_printf(b, "\\\\");
        else if (*p == '\n') sb_printf(b, "\\n");
        else if (*p == '\t') sb_printf(b, "\\t");
        else if (*p == '\r') sb_printf(b, "\\r");
        else if (*p < 0x20) sb_printf(b, "\\u%04x", (unsigned)*p);
        else sb_printf(b, "%c", (int)*p);
    }
    sb_printf(b, "\"");
}

static const char *null_scope_name(int level) {
    switch (level) {
    case CG_NULL_STRATUM: return "stratum";
    case CG_NULL_LECT_PAIR: return "lect_pair";
    case CG_NULL_SPARSE: return "sparse";
    default: return "none";
    }
}

static cg_status sb_finish(sbuf *b, char **out) {
    if (b->failed) {
        free(b->data);
        return CG_ERR_OOM;
    }
    if (b->data == NULL) {
        b->data = cg_strdup("");
        if (b->data == NULL) return CG_ERR_OOM;
    }
    *out = b->data;
    return CG_OK;
}

cg_status cg_result_pairs_tsv(const cg_result *result, char **out) {
    sbuf b = {0};
    size_t i;
    int morphemes;
    if (result == NULL || out == NULL || !result->has_pairs) return CG_ERR_INVALID_ARGUMENT;
    *out = NULL;
    morphemes = result->pairs_summary.unit == CG_PAIRS_MORPHEMES;
    if (morphemes) {
        sb_printf(&b, "form_a\tmorph_a\tform_b\tmorph_b\tweight\tdistance\tglobal\tlocal\toverlap\tnull_n\tnull_scope\n");
    } else {
        sb_printf(&b, "form_a\tform_b\tweight\tdistance\tglobal\tlocal\toverlap\tnull_n\tnull_scope\n");
    }
    for (i = 0; i < result->pair_count && !b.failed; i++) {
        const cg_pair_row *r = &result->pairs[i];
        if (morphemes) {
            sb_printf(&b, "%s\t%llu\t%s\t%llu\t", r->form_a, (unsigned long long)r->morph_a, r->form_b, (unsigned long long)r->morph_b);
        } else {
            sb_printf(&b, "%s\t%s\t", r->form_a, r->form_b);
        }
        sb_printf(&b, "%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%llu\t%s\n", r->weight, r->distance, r->global_distance, r->local_distance, r->overlap_distance, (unsigned long long)r->null_count, null_scope_name(r->null_level));
    }
    return sb_finish(&b, out);
}

cg_status cg_result_pairs_json(const cg_result *result, char **out) {
    sbuf b = {0};
    const cg_pairs_summary *s;
    size_t i;
    int morphemes;
    if (result == NULL || out == NULL || !result->has_pairs) return CG_ERR_INVALID_ARGUMENT;
    *out = NULL;
    s = &result->pairs_summary;
    morphemes = s->unit == CG_PAIRS_MORPHEMES;
    sb_printf(&b, "{\n  \"schema_version\": \"%s\",\n  \"role\": \"q\",\n", CG_PAIRS_SCHEMA_VERSION);
    sb_printf(&b, "  \"producer\": \"cognator@%s+merkmal.%s\",\n", cg_version_string(), cg_merkmal_commit());
    sb_printf(&b, "  \"unit\": \"%s\",\n  \"pairs\": [", morphemes ? "morpheme" : "word");
    for (i = 0; i < result->pair_count && !b.failed; i++) {
        const cg_pair_row *r = &result->pairs[i];
        sb_printf(&b, "%s\n    {\"form_obs_a\": ", i == 0 ? "" : ",");
        if (morphemes) {
            char suffix[32];
            size_t la = strlen(r->form_a);
            char *id = (char *)malloc(la + sizeof(suffix));
            if (id == NULL) {
                b.failed = 1;
                break;
            }
            snprintf(suffix, sizeof(suffix), "#%llu", (unsigned long long)r->morph_a);
            memcpy(id, r->form_a, la);
            strcpy(id + la, suffix);
            sb_json_string(&b, id);
            free(id);
            sb_printf(&b, ", \"form_obs_b\": ");
            id = (char *)malloc(strlen(r->form_b) + sizeof(suffix));
            if (id == NULL) {
                b.failed = 1;
                break;
            }
            snprintf(suffix, sizeof(suffix), "#%llu", (unsigned long long)r->morph_b);
            strcpy(id, r->form_b);
            strcat(id, suffix);
            sb_json_string(&b, id);
            free(id);
        } else {
            sb_json_string(&b, r->form_a);
            sb_printf(&b, ", \"form_obs_b\": ");
            sb_json_string(&b, r->form_b);
        }
        sb_printf(&b, ", \"weight\": %.6f, \"components\": {\"distance\": %.6f, \"global\": %.6f, \"local\": %.6f, \"overlap\": %.6f, \"null_n\": %llu, \"null_scope\": \"%s\"}}",
                  r->weight, r->distance, r->global_distance, r->local_distance, r->overlap_distance,
                  (unsigned long long)r->null_count, null_scope_name(r->null_level));
    }
    sb_printf(&b, "%s],\n", result->pair_count > 0 ? "\n  " : "");
    sb_printf(&b, "  \"diagnostics\": {\n");
    sb_printf(&b, "    \"cognator_version\": \"%s\",\n", cg_version_string());
    sb_printf(&b, "    \"merkmal\": {\"version\": \"%s\", \"commit\": \"%s\"},\n", cg_merkmal_version(), cg_merkmal_commit());
    sb_printf(&b, "    \"threshold\": %.6f,\n", s->threshold);
    if (morphemes && result->morpheme_ids != NULL) {
        sb_printf(&b, "    \"morpheme_threshold\": %.6f,\n", s->morpheme_threshold);
    }
    sb_printf(&b, "    \"units\": %llu,\n", (unsigned long long)s->morpheme_count);
    sb_printf(&b, "    \"weight\": {\n");
    sb_printf(&b, "      \"definition\": \"1 - share of cross-concept pairs of units from the same lect pair, in the same length bins, whose distance is <= this pair's distance\",\n");
    sb_printf(&b, "      \"reading\": \"a background-match rate (one minus a p-value-like quantity); not a calibrated posterior probability of cognacy\",\n");
    sb_printf(&b, "      \"length_bins\": \"1,2,3,4,5,6,7-8,9-10,11-13,14+ segments, per unit, matched on both sides\",\n");
    sb_printf(&b, "      \"scorer\": \"per-lect-pair correspondence tables trained on whole words; alignment and combined distance as in clustering\"\n");
    sb_printf(&b, "    },\n");
    sb_printf(&b, "    \"null\": {\"samples_per_stratum\": %d, \"min_stratum\": %d, \"seed\": %llu, \"strata\": %llu, \"null_pairs\": %llu},\n",
              s->null_samples, s->min_null, (unsigned long long)s->null_seed, (unsigned long long)s->strata, (unsigned long long)s->null_pairs);
    sb_printf(&b, "    \"calibration\": {\"pairs\": %llu, \"by_null_scope\": {\"stratum\": %llu, \"lect_pair\": %llu, \"sparse\": %llu, \"none\": %llu}, \"mean_weight\": %.6f, \"weight_ge_0_5\": %llu, \"weight_ge_0_95\": %llu},\n",
              (unsigned long long)s->pair_count,
              (unsigned long long)s->level_counts[CG_NULL_STRATUM], (unsigned long long)s->level_counts[CG_NULL_LECT_PAIR],
              (unsigned long long)s->level_counts[CG_NULL_SPARSE], (unsigned long long)s->level_counts[CG_NULL_NONE],
              s->mean_weight, (unsigned long long)s->weight_ge_050, (unsigned long long)s->weight_ge_095);
    sb_printf(&b, "    \"note\": \"proposal mass for a downstream model to reweight; weights are not cognacy and must not be read as hard cognate judgements\"\n");
    sb_printf(&b, "  }\n}\n");
    return sb_finish(&b, out);
}

cg_status cg_write_pairs(const char *path, const cg_result *result, int json) {
    char *text = NULL;
    FILE *f;
    cg_status status;
    size_t len;
    if (path == NULL || result == NULL) return CG_ERR_INVALID_ARGUMENT;
    status = json ? cg_result_pairs_json(result, &text) : cg_result_pairs_tsv(result, &text);
    if (status != CG_OK) return status;
    f = fopen(path, "wb");
    if (f == NULL) {
        free(text);
        return CG_ERR_IO;
    }
    len = strlen(text);
    if (fwrite(text, 1, len, f) != len) {
        fclose(f);
        free(text);
        return CG_ERR_IO;
    }
    free(text);
    return fclose(f) == 0 ? CG_OK : CG_ERR_IO;
}

cg_status cg_write_sidecar_morphemes(const char *path, const cg_result *result) {
    if (result == NULL || result->morpheme_ids == NULL) return CG_ERR_INVALID_ARGUMENT;
    return cg_internal_write_sidecar_rows(path, result->rows, (uint64_t)result->row_count, "Cognator_Morpheme_IDs", (const char *const *)result->morpheme_ids);
}
