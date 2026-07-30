#include "internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *mk_kind_name(mk_system_type kind)
{
    switch (kind) {
    case MK_SYSTEM_CATEGORICAL:
        return "categorical";
    case MK_SYSTEM_VALUED:
        return "valued";
    case MK_SYSTEM_TRAINED:
        return "trained";
    default:
        return "unknown";
    }
}

static size_t mk_utf8_char_len_local(unsigned char c)
{
    if (c < 0x80) {
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

static int mk_has_prefix_local(const char *s, const char *prefix)
{
    size_t n;

    if (s == NULL || prefix == NULL) {
        return 0;
    }
    n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

static mk_status mk_append_text(char **text, size_t *len, size_t *cap, const char *suffix)
{
    size_t n;
    char *next;

    n = strlen(suffix);
    if (*len + n + 1 > *cap) {
        size_t new_cap = *cap == 0 ? 32 : *cap * 2;
        while (*len + n + 1 > new_cap) {
            new_cap *= 2;
        }
        next = (char *)realloc(*text, new_cap);
        if (next == NULL) {
            return MK_ERR_OOM;
        }
        *text = next;
        *cap = new_cap;
    }
    memcpy(*text + *len, suffix, n);
    *len += n;
    (*text)[*len] = '\0';
    return MK_OK;
}

mk_status mk_system_name(const mk_system *system, const char **out)
{
    if (system == NULL || system->builtin == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = system->builtin->name;
    return MK_OK;
}

mk_status mk_system_kind(const mk_system *system, const char **out)
{
    if (system == NULL || system->builtin == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = mk_kind_name(system->builtin->kind);
    return MK_OK;
}

static const mk_builtin_entry *mk_find_entry(
    const mk_builtin_system *system,
    const char *key
)
{
    size_t i;

    for (i = 0; i < system->entry_count; i++) {
        if (mk_streq(system->entries[i].grapheme, key)) {
            return &system->entries[i];
        }
    }
    return NULL;
}

static char *mk_remove_tie_bars(const char *text)
{
    char *out = NULL;
    size_t len = 0;
    size_t cap = 0;
    const char *p = text;

    while (*p != '\0') {
        if (strncmp(p, "͡", strlen("͡")) == 0) {
            p += strlen("͡");
        } else if (strncmp(p, "͜", strlen("͜")) == 0) {
            p += strlen("͜");
        } else {
            char one[5];
            size_t n;
            unsigned char c = (unsigned char)*p;
            if (c < 0x80) {
                n = 1;
            } else if ((c & 0xE0) == 0xC0) {
                n = 2;
            } else if ((c & 0xF0) == 0xE0) {
                n = 3;
            } else if ((c & 0xF8) == 0xF0) {
                n = 4;
            } else {
                n = 1;
            }
            if (len + n + 1 > cap) {
                size_t new_cap = cap == 0 ? 32 : cap * 2;
                char *next;
                while (len + n + 1 > new_cap) {
                    new_cap *= 2;
                }
                next = (char *)realloc(out, new_cap);
                if (next == NULL) {
                    free(out);
                    return NULL;
                }
                out = next;
                cap = new_cap;
            }
            memcpy(one, p, n);
            memcpy(out + len, one, n);
            len += n;
            out[len] = '\0';
            p += n;
        }
    }
    if (out == NULL) {
        out = mk_strdup_internal("");
    }
    return out;
}

static char *mk_insert_affricate_retraction(const char *text)
{
    if (mk_streq(text, "tʃ")) {
        return mk_strdup_internal("t̠ʃ");
    }
    if (mk_streq(text, "dʒ")) {
        return mk_strdup_internal("d̠ʒ");
    }
    return mk_strdup_internal(text);
}

static mk_status mk_lookup_normalized(
    const mk_system *system,
    const char *normalized,
    const mk_builtin_entry **out
)
{
    char *without_tie;
    char *retracted;

    *out = mk_find_entry(system->builtin, normalized);
    if (*out != NULL) {
        return MK_OK;
    }

    without_tie = mk_remove_tie_bars(normalized);
    if (without_tie == NULL) {
        return MK_ERR_OOM;
    }
    if (!mk_streq(without_tie, normalized)) {
        *out = mk_find_entry(system->builtin, without_tie);
        if (*out != NULL) {
            free(without_tie);
            return MK_OK;
        }
    }

    retracted = mk_insert_affricate_retraction(without_tie);
    free(without_tie);
    if (retracted == NULL) {
        return MK_ERR_OOM;
    }
    *out = mk_find_entry(system->builtin, retracted);
    free(retracted);
    if (*out != NULL) {
        return MK_OK;
    }
    return MK_ERR_UNKNOWN_GRAPHEME;
}

mk_status mk_lookup_features(
    const mk_system *system,
    const char *utf8_grapheme,
    const mk_builtin_entry **out
)
{
    char *normalized;
    mk_status status;

    if (system == NULL || system->builtin == NULL || utf8_grapheme == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;

    status = mk_normalize_input_grapheme(utf8_grapheme, &normalized);
    if (status != MK_OK) {
        return status;
    }
    status = mk_lookup_normalized(system, normalized, out);
    mk_free_string(normalized);
    return status;
}

void mk_resolved_entry_clear(mk_resolved_entry *entry)
{
    size_t i;

    if (entry == NULL) {
        return;
    }
    for (i = 0; i < entry->owned_feature_count; i++) {
        free(entry->owned_features[i]);
    }
    free(entry->owned_features);
    free(entry->owned_grapheme);
    for (i = 0; i < entry->cluster_component_count; i++) {
        free(entry->cluster_components[i]);
    }
    free(entry->cluster_components);
    entry->grapheme = NULL;
    entry->features = NULL;
    entry->feature_count = 0;
    entry->owned_features = NULL;
    entry->owned_feature_count = 0;
    entry->owned_grapheme = NULL;
    entry->cluster_components = NULL;
    entry->cluster_component_count = 0;
}

static int mk_feature_list_contains(char **items, size_t count, const char *feature)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (mk_streq(items[i], feature)) {
            return 1;
        }
    }
    return 0;
}

static mk_status mk_add_owned_feature(char ***items, size_t *count, size_t *cap, const char *feature)
{
    char **next;

    if (feature == NULL || feature[0] == '\0' || mk_feature_list_contains(*items, *count, feature)) {
        return MK_OK;
    }
    if (*count + 1 > *cap) {
        size_t new_cap = *cap == 0 ? 8 : *cap * 2;
        next = (char **)realloc(*items, new_cap * sizeof(**items));
        if (next == NULL) {
            return MK_ERR_OOM;
        }
        *items = next;
        *cap = new_cap;
    }
    (*items)[*count] = mk_strdup_internal(feature);
    if ((*items)[*count] == NULL) {
        return MK_ERR_OOM;
    }
    (*count)++;
    return MK_OK;
}

static mk_status mk_replace_existing_valued_feature(
    char **items,
    size_t count,
    const char *feature,
    char state
)
{
    size_t i;
    size_t feature_len = strlen(feature);
    char *label;

    for (i = 0; i < count; i++) {
        if (strncmp(items[i], feature, feature_len) == 0 && items[i][feature_len] == '=') {
            label = (char *)malloc(feature_len + 3);
            if (label == NULL) {
                return MK_ERR_OOM;
            }
            memcpy(label, feature, feature_len);
            label[feature_len] = '=';
            label[feature_len + 1] = state;
            label[feature_len + 2] = '\0';
            free(items[i]);
            items[i] = label;
            return MK_OK;
        }
    }
    return MK_ERR_INVALID_ARGUMENT;
}

static mk_status mk_copy_entry_features(const mk_builtin_entry *entry, char ***items, size_t *count, size_t *cap)
{
    size_t i;
    mk_status status;

    for (i = 0; i < entry->feature_count; i++) {
        status = mk_add_owned_feature(items, count, cap, entry->features[i]);
        if (status != MK_OK) {
            return status;
        }
    }
    return MK_OK;
}

static int mk_feature_array_contains_exact(char **items, size_t count, const char *feature)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (mk_streq(items[i], feature)) {
            return 1;
        }
    }
    return 0;
}

static int mk_feature_array_marks_nucleus(char **items, size_t count)
{
    return mk_feature_array_contains_exact(items, count, "vowel") ||
        mk_feature_array_contains_exact(items, count, "syllabic") ||
        mk_feature_array_contains_exact(items, count, "syllabic=+");
}

static int mk_feature_array_marks_sonorant(char **items, size_t count)
{
    return mk_feature_array_contains_exact(items, count, "sonorant") ||
        mk_feature_array_contains_exact(items, count, "nasal") ||
        mk_feature_array_contains_exact(items, count, "lateral") ||
        mk_feature_array_contains_exact(items, count, "trill") ||
        mk_feature_array_contains_exact(items, count, "tap") ||
        mk_feature_array_contains_exact(items, count, "approximant");
}

static const char *mk_feature_dimension(const char *feature)
{
    if (mk_streq(feature, "close") ||
        mk_streq(feature, "near-close") ||
        mk_streq(feature, "close-mid") ||
        mk_streq(feature, "mid") ||
        mk_streq(feature, "open-mid") ||
        mk_streq(feature, "near-open") ||
        mk_streq(feature, "open")) {
        return "height";
    }
    if (mk_streq(feature, "front") ||
        mk_streq(feature, "near-front") ||
        mk_streq(feature, "central") ||
        mk_streq(feature, "near-back") ||
        mk_streq(feature, "back")) {
        return "centrality";
    }
    if (mk_streq(feature, "rounded") || mk_streq(feature, "unrounded")) {
        return "roundedness";
    }
    if (mk_streq(feature, "long") ||
        mk_streq(feature, "mid-long") ||
        mk_streq(feature, "ultra-long") ||
        mk_streq(feature, "ultra-short")) {
        return "duration";
    }
    if (mk_streq(feature, "nasalized")) {
        return "nasalization";
    }
    if (mk_streq(feature, "centralized") ||
        mk_streq(feature, "mid-centralized") ||
        mk_streq(feature, "advanced") ||
        mk_streq(feature, "retracted")) {
        return "relative";
    }
    if (mk_streq(feature, "non-syllabic") || mk_streq(feature, "syllabic")) {
        return "syllabicity";
    }
    return NULL;
}

static const char *mk_entry_feature_for_dimension(
    const mk_builtin_entry *entry,
    const char *dimension
)
{
    size_t i;

    for (i = 0; i < entry->feature_count; i++) {
        const char *candidate = entry->features[i];
        const char *candidate_dimension = mk_feature_dimension(candidate);
        if (candidate_dimension != NULL && mk_streq(candidate_dimension, dimension)) {
            return candidate;
        }
    }
    return NULL;
}

static mk_status mk_add_prefixed_feature(
    char ***items,
    size_t *count,
    size_t *cap,
    const char *prefix,
    const char *feature
)
{
    char label[96];

    if (feature == NULL || feature[0] == '\0') {
        return MK_OK;
    }
    snprintf(label, sizeof(label), "%s-%s", prefix, feature);
    return mk_add_owned_feature(items, count, cap, label);
}

static mk_status mk_add_movement_feature(
    char ***items,
    size_t *count,
    size_t *cap,
    const char *dimension,
    const char *from,
    const char *to
)
{
    char label[128];

    if (dimension == NULL || from == NULL || to == NULL) {
        return MK_OK;
    }
    snprintf(label, sizeof(label), "move-%s-%s-%s", dimension, from, to);
    return mk_add_owned_feature(items, count, cap, label);
}

static int mk_feature_base_present(char **items, size_t count, const char *feature)
{
    size_t i;
    size_t feature_len = strlen(feature);

    for (i = 0; i < count; i++) {
        if (strncmp(items[i], feature, feature_len) == 0 && items[i][feature_len] == '=') {
            return 1;
        }
    }
    return 0;
}

static const mk_diacritic_map *mk_match_diacritic_map(
    const mk_diacritic_map *map,
    size_t count,
    const char *text
)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (mk_has_prefix_local(text, map[i].mark)) {
            return &map[i];
        }
    }
    return NULL;
}

static const char *mk_match_extra_suffix_feature(const char *text, size_t *bytes_out)
{
    if (mk_has_prefix_local(text, "ʳ")) {
        *bytes_out = strlen("ʳ");
        return "rhotacized";
    }
    return NULL;
}

static const char *mk_match_extra_prefix_feature(const char *text, size_t *bytes_out)
{
    if (mk_has_prefix_local(text, "ᵐ")) {
        *bytes_out = strlen("ᵐ");
        return "pre-nasalized";
    }
    return NULL;
}

static const mk_tone_mark *mk_match_tone_mark(const char *text)
{
    size_t i;

    for (i = 0; i < mk_default_tone_mark_count; i++) {
        if (mk_has_prefix_local(text, mk_default_tone_marks[i].mark)) {
            return &mk_default_tone_marks[i];
        }
    }
    return NULL;
}

static int mk_chao_digit_value_local(const char *p)
{
    if (mk_has_prefix_local(p, "¹")) {
        return 1;
    }
    if (mk_has_prefix_local(p, "²")) {
        return 2;
    }
    if (mk_has_prefix_local(p, "³")) {
        return 3;
    }
    if (mk_has_prefix_local(p, "⁴")) {
        return 4;
    }
    if (mk_has_prefix_local(p, "⁵")) {
        return 5;
    }
    return -1;
}

static mk_status mk_add_chao_level_features(
    char ***modifiers,
    size_t *modifier_count,
    size_t *modifier_cap,
    const char *position,
    int level
)
{
    mk_status status;
    char feature[32];

    if (level == 3) {
        return MK_OK;
    }
    if (level < 1 || level > 5) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }

    if (level == 1 || level == 2) {
        snprintf(feature, sizeof(feature), "tone-%s-lower", position);
        status = mk_add_owned_feature(modifiers, modifier_count, modifier_cap, feature);
        if (status != MK_OK) {
            return status;
        }
    } else {
        snprintf(feature, sizeof(feature), "tone-%s-upper", position);
        status = mk_add_owned_feature(modifiers, modifier_count, modifier_cap, feature);
        if (status != MK_OK) {
            return status;
        }
    }

    snprintf(
        feature,
        sizeof(feature),
        "tone-%s-%s",
        position,
        level == 1 || level == 4 ? "lowered" : "raised"
    );
    return mk_add_owned_feature(modifiers, modifier_count, modifier_cap, feature);
}

static mk_status mk_add_chao_tone_features(
    char ***modifiers,
    size_t *modifier_count,
    size_t *modifier_cap,
    const int *levels,
    size_t level_count
)
{
    mk_status status;

    if (level_count == 1) {
        status = mk_add_chao_level_features(
            modifiers,
            modifier_count,
            modifier_cap,
            "onset",
            levels[0]
        );
        if (status != MK_OK) {
            return status;
        }
        status = mk_add_chao_level_features(
            modifiers,
            modifier_count,
            modifier_cap,
            "mid",
            levels[0]
        );
        if (status != MK_OK) {
            return status;
        }
        return mk_add_chao_level_features(
            modifiers,
            modifier_count,
            modifier_cap,
            "offset",
            levels[0]
        );
    }

    status = mk_add_chao_level_features(
        modifiers,
        modifier_count,
        modifier_cap,
        "onset",
        levels[0]
    );
    if (status != MK_OK) {
        return status;
    }
    if (level_count == 3) {
        status = mk_add_chao_level_features(
            modifiers,
            modifier_count,
            modifier_cap,
            "mid",
            levels[1]
        );
        if (status != MK_OK) {
            return status;
        }
    }
    return mk_add_chao_level_features(
        modifiers,
        modifier_count,
        modifier_cap,
        "offset",
        levels[level_count - 1]
    );
}

static mk_status mk_match_chao_tone_sequence(
    const char *text,
    size_t *bytes_out,
    char ***modifiers,
    size_t *modifier_count,
    size_t *modifier_cap
)
{
    const char *p = text;
    int levels[3];
    size_t count = 0;

    if (bytes_out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *bytes_out = 0;

    while (*p != '\0' && count < 3) {
        int value = mk_chao_digit_value_local(p);
        if (value < 1 || value > 5) {
            break;
        }
        levels[count++] = value;
        p += mk_utf8_char_len_local((unsigned char)*p);
    }

    if (count == 0) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }
    if (mk_chao_digit_value_local(p) >= 1) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }

    *bytes_out = (size_t)(p - text);
    return mk_add_chao_tone_features(modifiers, modifier_count, modifier_cap, levels, count);
}

static const mk_valued_diacritic_effect *mk_find_valued_effect(const char *modifier)
{
    size_t i;

    for (i = 0; i < mk_default_valued_diacritic_effect_count; i++) {
        if (mk_streq(mk_default_valued_diacritic_effects[i].modifier, modifier)) {
            return &mk_default_valued_diacritic_effects[i];
        }
    }
    return NULL;
}

static mk_status mk_decompose_diacritics(
    const char *normalized,
    char **base_out,
    char ***modifiers_out,
    size_t *modifier_count_out,
    int *recognized_modifier_out,
    int *tone_seen_out
)
{
    const char *p;
    char *base = NULL;
    size_t base_len = 0;
    size_t base_cap = 0;
    char **modifiers = NULL;
    size_t modifier_count = 0;
    size_t modifier_cap = 0;
    int recognized_modifier = 0;
    int tone_seen = 0;
    mk_status status = MK_OK;

    if (base_out == NULL ||
        modifiers_out == NULL ||
        modifier_count_out == NULL ||
        recognized_modifier_out == NULL ||
        tone_seen_out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *base_out = NULL;
    *modifiers_out = NULL;
    *modifier_count_out = 0;
    *recognized_modifier_out = 0;
    *tone_seen_out = 0;

    p = normalized;
    while (*p != '\0') {
        const mk_diacritic_map *prefix = mk_match_diacritic_map(
            mk_default_prefix_diacritics,
            mk_default_prefix_diacritic_count,
            p
        );
        const char *extra_prefix;
        size_t extra_prefix_bytes = 0;
        if (prefix == NULL) {
            extra_prefix = mk_match_extra_prefix_feature(p, &extra_prefix_bytes);
            if (extra_prefix == NULL) {
                break;
            }
            status = mk_add_owned_feature(&modifiers, &modifier_count, &modifier_cap, extra_prefix);
            if (status != MK_OK) {
                goto fail;
            }
            recognized_modifier = 1;
            p += extra_prefix_bytes;
            continue;
        }
        status = mk_add_owned_feature(&modifiers, &modifier_count, &modifier_cap, prefix->feature);
        if (status != MK_OK) {
            goto fail;
        }
        recognized_modifier = 1;
        p += strlen(prefix->mark);
    }

    while (*p != '\0') {
        const mk_tone_mark *tone = mk_match_tone_mark(p);
        const mk_diacritic_map *combining;
        const mk_diacritic_map *suffix;
        const char *extra_suffix;
        size_t extra_suffix_bytes = 0;
        size_t chao_bytes = 0;
        char one[5];
        size_t n;
        size_t i;

        if (tone != NULL) {
            for (i = 0; i < tone->feature_count; i++) {
                status = mk_add_owned_feature(&modifiers, &modifier_count, &modifier_cap, tone->features[i]);
                if (status != MK_OK) {
                    goto fail;
                }
            }
            recognized_modifier = 1;
            tone_seen = 1;
            p += strlen(tone->mark);
            continue;
        }

        status = mk_match_chao_tone_sequence(
            p,
            &chao_bytes,
            &modifiers,
            &modifier_count,
            &modifier_cap
        );
        if (status == MK_OK) {
            recognized_modifier = 1;
            tone_seen = 1;
            p += chao_bytes;
            continue;
        }
        if (status != MK_ERR_UNKNOWN_GRAPHEME) {
            goto fail;
        }

        combining = mk_match_diacritic_map(
            mk_default_combining_diacritics,
            mk_default_combining_diacritic_count,
            p
        );
        if (combining != NULL) {
            status = mk_add_owned_feature(&modifiers, &modifier_count, &modifier_cap, combining->feature);
            if (status != MK_OK) {
                goto fail;
            }
            recognized_modifier = 1;
            p += strlen(combining->mark);
            continue;
        }

        extra_suffix = mk_match_extra_suffix_feature(p, &extra_suffix_bytes);
        if (extra_suffix != NULL) {
            status = mk_add_owned_feature(&modifiers, &modifier_count, &modifier_cap, extra_suffix);
            if (status != MK_OK) {
                goto fail;
            }
            recognized_modifier = 1;
            p += extra_suffix_bytes;
            continue;
        }

        suffix = mk_match_diacritic_map(
            mk_default_suffix_diacritics,
            mk_default_suffix_diacritic_count,
            p
        );
        if (suffix != NULL) {
            status = mk_add_owned_feature(&modifiers, &modifier_count, &modifier_cap, suffix->feature);
            if (status != MK_OK) {
                goto fail;
            }
            recognized_modifier = 1;
            p += strlen(suffix->mark);
            continue;
        }

        n = mk_utf8_char_len_local((unsigned char)*p);
        memcpy(one, p, n);
        one[n] = '\0';
        status = mk_append_text(&base, &base_len, &base_cap, one);
        if (status != MK_OK) {
            goto fail;
        }
        p += n;
    }

    if (base == NULL) {
        base = mk_strdup_internal("");
        if (base == NULL) {
            status = MK_ERR_OOM;
            goto fail;
        }
    }

    *base_out = base;
    *modifiers_out = modifiers;
    *modifier_count_out = modifier_count;
    *recognized_modifier_out = recognized_modifier;
    *tone_seen_out = tone_seen;
    return MK_OK;

fail:
    free(base);
    if (modifiers != NULL) {
        size_t j;
        for (j = 0; j < modifier_count; j++) {
            free(modifiers[j]);
        }
        free(modifiers);
    }
    return status;
}

static void mk_free_owned_feature_array(char **features, size_t count)
{
    size_t i;

    if (features == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(features[i]);
    }
    free(features);
}

static mk_status mk_apply_valued_modifiers(
    char **features,
    size_t count,
    char **modifiers,
    size_t modifier_count
)
{
    size_t i;

    for (i = 0; i < modifier_count; i++) {
        const mk_valued_diacritic_effect *effect = mk_find_valued_effect(modifiers[i]);
        size_t j;
        if (effect == NULL) {
            continue;
        }
        for (j = 0; j < effect->alternative_count; j++) {
            const char *alternative = effect->alternatives[j];
            if (mk_feature_base_present(features, count, alternative)) {
                mk_status status = mk_replace_existing_valued_feature(
                    features,
                    count,
                    alternative,
                    effect->state
                );
                if (status != MK_OK) {
                    return status;
                }
                break;
            }
        }
    }
    return MK_OK;
}

static int mk_is_descriptive_system(const mk_system *system)
{
    return system != NULL &&
        system->builtin != NULL &&
        system->builtin->kind == MK_SYSTEM_CATEGORICAL &&
        mk_streq(system->builtin->name, "descriptive");
}

static mk_status mk_synthesize_from_diacritics(
    const mk_system *system,
    const char *normalized,
    mk_resolved_entry *out
);

static mk_status mk_set_synthesized_entry(
    mk_resolved_entry *out,
    const char *grapheme,
    char **features,
    size_t count,
    char **components,
    size_t component_count
)
{
    out->owned_grapheme = mk_strdup_internal(grapheme);
    if (out->owned_grapheme == NULL) {
        return MK_ERR_OOM;
    }
    out->grapheme = out->owned_grapheme;
    out->owned_features = features;
    out->owned_feature_count = count;
    out->features = (const char *const *)features;
    out->feature_count = count;
    out->cluster_components = components;
    out->cluster_component_count = component_count;
    return MK_OK;
}

static mk_status mk_add_cluster_component(
    char ***components,
    size_t *count,
    size_t *cap,
    const char *start,
    size_t len
)
{
    char **next;
    char *component;

    if (*count + 1 > *cap) {
        size_t new_cap = *cap == 0 ? 4 : *cap * 2;
        next = (char **)realloc(*components, new_cap * sizeof(**components));
        if (next == NULL) {
            return MK_ERR_OOM;
        }
        *components = next;
        *cap = new_cap;
    }
    component = (char *)malloc(len + 1);
    if (component == NULL) {
        return MK_ERR_OOM;
    }
    memcpy(component, start, len);
    component[len] = '\0';
    (*components)[*count] = component;
    (*count)++;
    return MK_OK;
}

static void mk_free_cluster_components(char **components, size_t count)
{
    size_t i;

    if (components == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(components[i]);
    }
    free(components);
}

static mk_status mk_add_position_features(
    char ***features,
    size_t *count,
    size_t *cap,
    const mk_resolved_entry *component,
    size_t position
)
{
    size_t i;
    char prefix[8];

    snprintf(prefix, sizeof(prefix), "n%zu", position + 1);
    for (i = 0; i < component->feature_count; i++) {
        const char *feature = component->features[i];
        if (mk_streq(feature, "vowel") || mk_has_prefix_local(feature, "tone-")) {
            continue;
        }
        if (mk_add_prefixed_feature(features, count, cap, prefix, feature) != MK_OK) {
            return MK_ERR_OOM;
        }
    }
    return MK_OK;
}

static mk_status mk_add_cluster_movement_features(
    char ***features,
    size_t *count,
    size_t *cap,
    const mk_resolved_entry *from,
    const mk_resolved_entry *to
)
{
    static const char *const dimensions[] = {
        "height",
        "centrality",
        "roundedness"
    };
    size_t i;

    for (i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); i++) {
        mk_builtin_entry a;
        mk_builtin_entry b;
        const char *from_feature;
        const char *to_feature;

        a.grapheme = from->grapheme;
        a.features = from->features;
        a.feature_count = from->feature_count;
        b.grapheme = to->grapheme;
        b.features = to->features;
        b.feature_count = to->feature_count;
        from_feature = mk_entry_feature_for_dimension(&a, dimensions[i]);
        to_feature = mk_entry_feature_for_dimension(&b, dimensions[i]);
        if (mk_add_movement_feature(features, count, cap, dimensions[i], from_feature, to_feature) != MK_OK) {
            return MK_ERR_OOM;
        }
    }
    return MK_OK;
}

static mk_status mk_parse_component_at(
    const mk_system *system,
    const char *start,
    const char **end_out,
    mk_resolved_entry *component_out
)
{
    const char *p = start;
    char *component = NULL;
    size_t len = 0;
    size_t cap = 0;
    mk_status status;

    if (*p == '\0' || mk_chao_digit_value_local(p) >= 1 || mk_match_tone_mark(p) != NULL) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }
    if (!(*p == 'a' ||
        *p == 'e' ||
        *p == 'i' ||
        *p == 'o' ||
        *p == 'u' ||
        mk_has_prefix_local(p, "y") ||
        mk_has_prefix_local(p, "ɛ") ||
        mk_has_prefix_local(p, "ɔ") ||
        mk_has_prefix_local(p, "ə") ||
        mk_has_prefix_local(p, "ɐ") ||
        mk_has_prefix_local(p, "ɨ") ||
        mk_has_prefix_local(p, "ʉ") ||
        mk_has_prefix_local(p, "ɯ") ||
        mk_has_prefix_local(p, "ɵ") ||
        mk_has_prefix_local(p, "œ") ||
        mk_has_prefix_local(p, "æ") ||
        mk_has_prefix_local(p, "ɑ") ||
        mk_has_prefix_local(p, "ʌ") ||
        mk_has_prefix_local(p, "ɪ") ||
        mk_has_prefix_local(p, "ʊ") ||
        mk_has_prefix_local(p, "ɤ") ||
        mk_has_prefix_local(p, "ø") ||
        mk_has_prefix_local(p, "ɘ") ||
        mk_has_prefix_local(p, "ɜ") ||
        mk_has_prefix_local(p, "ɞ") ||
        mk_has_prefix_local(p, "ɒ") ||
        mk_has_prefix_local(p, "ɶ") ||
        mk_has_prefix_local(p, "ɿ") ||
        mk_has_prefix_local(p, "ʅ"))) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }

    status = mk_append_text(&component, &len, &cap, "");
    if (status != MK_OK) {
        return status;
    }
    {
        char one[5];
        size_t n = mk_utf8_char_len_local((unsigned char)*p);
        memcpy(one, p, n);
        one[n] = '\0';
        status = mk_append_text(&component, &len, &cap, one);
        if (status != MK_OK) {
            free(component);
            return status;
        }
        p += n;
    }

    while (*p != '\0') {
        const mk_diacritic_map *combining;
        const mk_diacritic_map *suffix;
        const char *extra_suffix;
        size_t extra_suffix_bytes = 0;

        if (mk_chao_digit_value_local(p) >= 1 || mk_match_tone_mark(p) != NULL) {
            break;
        }
        combining = mk_match_diacritic_map(
            mk_default_combining_diacritics,
            mk_default_combining_diacritic_count,
            p
        );
        if (combining != NULL) {
            status = mk_append_text(&component, &len, &cap, combining->mark);
            if (status != MK_OK) {
                free(component);
                return status;
            }
            p += strlen(combining->mark);
            continue;
        }
        extra_suffix = mk_match_extra_suffix_feature(p, &extra_suffix_bytes);
        if (extra_suffix != NULL) {
            char mark[5];
            memcpy(mark, p, extra_suffix_bytes);
            mark[extra_suffix_bytes] = '\0';
            status = mk_append_text(&component, &len, &cap, mark);
            if (status != MK_OK) {
                free(component);
                return status;
            }
            p += extra_suffix_bytes;
            continue;
        }
        suffix = mk_match_diacritic_map(
            mk_default_suffix_diacritics,
            mk_default_suffix_diacritic_count,
            p
        );
        if (suffix != NULL) {
            status = mk_append_text(&component, &len, &cap, suffix->mark);
            if (status != MK_OK) {
                free(component);
                return status;
            }
            p += strlen(suffix->mark);
            continue;
        }
        break;
    }

    {
        const mk_builtin_entry *entry = NULL;
        status = mk_lookup_normalized(system, component, &entry);
        if (status == MK_OK) {
            component_out->grapheme = entry->grapheme;
            component_out->features = entry->features;
            component_out->feature_count = entry->feature_count;
        } else if (status == MK_ERR_UNKNOWN_GRAPHEME) {
            status = mk_synthesize_from_diacritics(system, component, component_out);
        }
    }
    if (status != MK_OK) {
        free(component);
        return status;
    }
    if (!mk_feature_array_marks_nucleus((char **)component_out->features, component_out->feature_count) ||
        !mk_feature_array_contains_exact((char **)component_out->features, component_out->feature_count, "vowel")) {
        mk_resolved_entry_clear(component_out);
        free(component);
        return MK_ERR_UNKNOWN_GRAPHEME;
    }
    *end_out = p;
    free(component);
    return MK_OK;
}

static mk_status mk_synthesize_vowel_cluster(
    const mk_system *system,
    const char *normalized,
    mk_resolved_entry *out
)
{
    const char *p;
    mk_resolved_entry components[3];
    size_t component_count = 0;
    char **component_names = NULL;
    size_t component_name_count = 0;
    size_t component_name_cap = 0;
    char **features = NULL;
    size_t feature_count = 0;
    size_t feature_cap = 0;
    mk_status status = MK_ERR_UNKNOWN_GRAPHEME;
    size_t i;

    memset(components, 0, sizeof(components));
    if (!mk_is_descriptive_system(system)) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }

    p = normalized;
    while (*p != '\0') {
        const char *next = NULL;
        size_t chao_bytes = 0;
        const mk_tone_mark *tone = mk_match_tone_mark(p);

        if (tone != NULL) {
            if (component_count < 2 || p[strlen(tone->mark)] != '\0') {
                status = MK_ERR_UNKNOWN_GRAPHEME;
                goto finish;
            }
            for (i = 0; i < tone->feature_count; i++) {
                status = mk_add_owned_feature(&features, &feature_count, &feature_cap, tone->features[i]);
                if (status != MK_OK) {
                    goto finish;
                }
            }
            p += strlen(tone->mark);
            break;
        }

        status = mk_match_chao_tone_sequence(
            p,
            &chao_bytes,
            &features,
            &feature_count,
            &feature_cap
        );
        if (status == MK_OK) {
            if (component_count < 2 || p[chao_bytes] != '\0') {
                status = MK_ERR_UNKNOWN_GRAPHEME;
                goto finish;
            }
            p += chao_bytes;
            break;
        }
        if (status != MK_ERR_UNKNOWN_GRAPHEME) {
            goto finish;
        }

        if (component_count == 3) {
            status = MK_ERR_UNKNOWN_GRAPHEME;
            goto finish;
        }
        status = mk_parse_component_at(system, p, &next, &components[component_count]);
        if (status != MK_OK) {
            goto finish;
        }
        status = mk_add_cluster_component(
            &component_names,
            &component_name_count,
            &component_name_cap,
            p,
            (size_t)(next - p)
        );
        if (status != MK_OK) {
            goto finish;
        }
        component_count++;
        p = next;
    }

    if (component_count < 2 || component_count > 3) {
        status = MK_ERR_UNKNOWN_GRAPHEME;
        goto finish;
    }
    status = mk_add_owned_feature(&features, &feature_count, &feature_cap, "vowel");
    if (status != MK_OK) {
        goto finish;
    }
    status = mk_add_owned_feature(
        &features,
        &feature_count,
        &feature_cap,
        component_count == 2 ? "diphthong" : "triphthong"
    );
    if (status != MK_OK) {
        goto finish;
    }
    for (i = 0; i < component_count; i++) {
        status = mk_add_position_features(&features, &feature_count, &feature_cap, &components[i], i);
        if (status != MK_OK) {
            goto finish;
        }
    }
    for (i = 1; i < component_count; i++) {
        status = mk_add_cluster_movement_features(&features, &feature_count, &feature_cap, &components[i - 1], &components[i]);
        if (status != MK_OK) {
            goto finish;
        }
    }
    status = mk_set_synthesized_entry(out, normalized, features, feature_count, component_names, component_name_count);
    if (status == MK_OK) {
        features = NULL;
        component_names = NULL;
        component_name_count = 0;
    }

finish:
    for (i = 0; i < component_count; i++) {
        mk_resolved_entry_clear(&components[i]);
    }
    mk_free_owned_feature_array(features, feature_count);
    mk_free_cluster_components(component_names, component_name_count);
    return status;
}

static mk_status mk_synthesize_descriptive_complex(
    const mk_system *system,
    const char *normalized,
    mk_resolved_entry *out
)
{
    char **features = NULL;
    size_t count = 0;
    size_t cap = 0;
    const char *place = NULL;
    const char *phonation = NULL;
    mk_status status;

    if (!mk_is_descriptive_system(system)) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }

    if (mk_streq(normalized, "kp")) {
        place = "labio-velar";
        phonation = "voiceless";
    } else if (mk_streq(normalized, "gb")) {
        place = "labio-velar";
        phonation = "voiced";
    } else if (mk_streq(normalized, "kx")) {
        place = "velar";
        phonation = "voiceless";
    } else if (mk_streq(normalized, "gɣ")) {
        place = "velar";
        phonation = "voiced";
    } else if (mk_streq(normalized, "kɣ")) {
        place = "velar";
    } else if (mk_streq(normalized, "ts")) {
        place = "alveolar";
        phonation = "voiceless";
    } else if (mk_streq(normalized, "dz")) {
        place = "alveolar";
        phonation = "voiced";
    } else if (mk_streq(normalized, "tʃ")) {
        place = "post-alveolar";
        phonation = "voiceless";
    } else if (mk_streq(normalized, "dʒ")) {
        place = "post-alveolar";
        phonation = "voiced";
    } else if (mk_streq(normalized, "tɕ")) {
        place = "alveolo-palatal";
        phonation = "voiceless";
    } else if (mk_streq(normalized, "dʑ")) {
        place = "alveolo-palatal";
        phonation = "voiced";
    } else if (mk_streq(normalized, "tʂ")) {
        place = "retroflex";
        phonation = "voiceless";
    } else if (mk_streq(normalized, "dʐ")) {
        place = "retroflex";
        phonation = "voiced";
    } else {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }

    status = mk_add_owned_feature(&features, &count, &cap, "consonant");
    if (status != MK_OK) {
        goto fail;
    }
    status = mk_add_owned_feature(&features, &count, &cap, place);
    if (status != MK_OK) {
        goto fail;
    }
    if (mk_streq(normalized, "kp") || mk_streq(normalized, "gb")) {
        status = mk_add_owned_feature(&features, &count, &cap, "stop");
    } else {
        status = mk_add_owned_feature(&features, &count, &cap, "affricate");
    }
    if (status != MK_OK) {
        goto fail;
    }
    status = mk_add_owned_feature(&features, &count, &cap, phonation);
    if (status != MK_OK) {
        goto fail;
    }
    if (!mk_streq(place, "velar") && !mk_streq(normalized, "kp") && !mk_streq(normalized, "gb")) {
        status = mk_add_owned_feature(&features, &count, &cap, "sibilant");
        if (status != MK_OK) {
            goto fail;
        }
    }
    status = mk_set_synthesized_entry(out, normalized, features, count, NULL, 0);
    if (status == MK_OK) {
        return MK_OK;
    }

fail:
    mk_free_owned_feature_array(features, count);
    return status;
}

static mk_status mk_parse_consonant_component_at(
    const mk_system *system,
    const char *start,
    const char **end_out,
    mk_resolved_entry *component_out
)
{
    const char *p = start;
    char *component = NULL;
    size_t len = 0;
    size_t cap = 0;
    mk_status status;

    if (*p == '\0' ||
        mk_chao_digit_value_local(p) >= 1 ||
        mk_match_tone_mark(p) != NULL ||
        *p == '<' ||
        *p == '>' ||
        *p == '+' ||
        *p == '-' ||
        *p == '/' ||
        *p == '[' ||
        *p == ']' ||
        mk_has_prefix_local(p, "→") ||
        mk_has_prefix_local(p, "∼")) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }

    status = mk_append_text(&component, &len, &cap, "");
    if (status != MK_OK) {
        return status;
    }
    {
        char one[5];
        size_t n = mk_utf8_char_len_local((unsigned char)*p);
        memcpy(one, p, n);
        one[n] = '\0';
        status = mk_append_text(&component, &len, &cap, one);
        if (status != MK_OK) {
            free(component);
            return status;
        }
        p += n;
    }

    while (*p != '\0') {
        const mk_diacritic_map *combining;
        const mk_diacritic_map *suffix;
        const char *extra_suffix;
        size_t extra_suffix_bytes = 0;

        if (mk_chao_digit_value_local(p) >= 1 || mk_match_tone_mark(p) != NULL) {
            break;
        }
        combining = mk_match_diacritic_map(
            mk_default_combining_diacritics,
            mk_default_combining_diacritic_count,
            p
        );
        if (combining != NULL) {
            status = mk_append_text(&component, &len, &cap, combining->mark);
            if (status != MK_OK) {
                free(component);
                return status;
            }
            p += strlen(combining->mark);
            continue;
        }
        extra_suffix = mk_match_extra_suffix_feature(p, &extra_suffix_bytes);
        if (extra_suffix != NULL) {
            char mark[5];
            memcpy(mark, p, extra_suffix_bytes);
            mark[extra_suffix_bytes] = '\0';
            status = mk_append_text(&component, &len, &cap, mark);
            if (status != MK_OK) {
                free(component);
                return status;
            }
            p += extra_suffix_bytes;
            continue;
        }
        suffix = mk_match_diacritic_map(
            mk_default_suffix_diacritics,
            mk_default_suffix_diacritic_count,
            p
        );
        if (suffix != NULL) {
            status = mk_append_text(&component, &len, &cap, suffix->mark);
            if (status != MK_OK) {
                free(component);
                return status;
            }
            p += strlen(suffix->mark);
            continue;
        }
        break;
    }

    {
        const mk_builtin_entry *entry = NULL;
        status = mk_lookup_normalized(system, component, &entry);
        if (status == MK_OK) {
            component_out->grapheme = entry->grapheme;
            component_out->features = entry->features;
            component_out->feature_count = entry->feature_count;
        } else if (status == MK_ERR_UNKNOWN_GRAPHEME) {
            status = mk_synthesize_from_diacritics(system, component, component_out);
        }
    }
    if (status != MK_OK) {
        free(component);
        return status;
    }
    if (!mk_feature_array_contains_exact((char **)component_out->features, component_out->feature_count, "consonant") ||
        mk_feature_array_contains_exact((char **)component_out->features, component_out->feature_count, "vowel")) {
        mk_resolved_entry_clear(component_out);
        free(component);
        return MK_ERR_UNKNOWN_GRAPHEME;
    }
    *end_out = p;
    free(component);
    return MK_OK;
}

static mk_status mk_synthesize_descriptive_consonant_cluster(
    const mk_system *system,
    const char *normalized,
    mk_resolved_entry *out
)
{
    const char *p;
    mk_resolved_entry components[3];
    size_t component_count = 0;
    char **component_names = NULL;
    size_t component_name_count = 0;
    size_t component_name_cap = 0;
    char **features = NULL;
    size_t feature_count = 0;
    size_t feature_cap = 0;
    mk_status status = MK_ERR_UNKNOWN_GRAPHEME;
    size_t i;

    memset(components, 0, sizeof(components));
    if (!mk_is_descriptive_system(system)) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }
    if (mk_streq(normalized, "mb") || mk_streq(normalized, "nd")) {
        return MK_ERR_UNKNOWN_GRAPHEME;
    }

    p = normalized;
    while (*p != '\0') {
        const char *next = NULL;

        if (component_count == 3) {
            status = MK_ERR_UNKNOWN_GRAPHEME;
            goto finish;
        }
        status = mk_parse_consonant_component_at(system, p, &next, &components[component_count]);
        if (status != MK_OK || next == p) {
            status = MK_ERR_UNKNOWN_GRAPHEME;
            goto finish;
        }
        status = mk_add_cluster_component(
            &component_names,
            &component_name_count,
            &component_name_cap,
            p,
            (size_t)(next - p)
        );
        if (status != MK_OK) {
            goto finish;
        }
        component_count++;
        p = next;
    }

    if (component_count < 2 || component_count > 3) {
        status = MK_ERR_UNKNOWN_GRAPHEME;
        goto finish;
    }
    status = mk_add_owned_feature(&features, &feature_count, &feature_cap, "consonant");
    if (status != MK_OK) {
        goto finish;
    }
    status = mk_add_owned_feature(&features, &feature_count, &feature_cap, "complex");
    if (status != MK_OK) {
        goto finish;
    }
    status = mk_add_owned_feature(&features, &feature_count, &feature_cap, "consonant-cluster");
    if (status != MK_OK) {
        goto finish;
    }
    if (component_count == 2 && mk_streq(component_names[0], component_names[1])) {
        status = mk_add_owned_feature(&features, &feature_count, &feature_cap, "geminate");
        if (status != MK_OK) {
            goto finish;
        }
    }
    if (mk_feature_array_contains_exact((char **)components[0].features, components[0].feature_count, "nasal")) {
        status = mk_add_owned_feature(&features, &feature_count, &feature_cap, "pre-nasalized");
        if (status != MK_OK) {
            goto finish;
        }
    }
    for (i = 0; i < component_count; i++) {
        status = mk_add_position_features(&features, &feature_count, &feature_cap, &components[i], i);
        if (status != MK_OK) {
            goto finish;
        }
    }

    status = mk_set_synthesized_entry(out, normalized, features, feature_count, component_names, component_name_count);
    if (status == MK_OK) {
        features = NULL;
        component_names = NULL;
        component_name_count = 0;
    }

finish:
    for (i = 0; i < component_count; i++) {
        mk_resolved_entry_clear(&components[i]);
    }
    mk_free_owned_feature_array(features, feature_count);
    mk_free_cluster_components(component_names, component_name_count);
    return status;
}

static mk_status mk_synthesize_from_diacritics(
    const mk_system *system,
    const char *normalized,
    mk_resolved_entry *out
)
{
    char *base = NULL;
    char **modifiers = NULL;
    size_t modifier_count = 0;
    int recognized_modifier = 0;
    int tone_seen = 0;
    const mk_builtin_entry *base_entry = NULL;
    mk_resolved_entry base_resolved;
    char **features = NULL;
    size_t count = 0;
    size_t cap = 0;
    mk_status status;
    size_t i;

    memset(&base_resolved, 0, sizeof(base_resolved));
    status = mk_decompose_diacritics(
        normalized,
        &base,
        &modifiers,
        &modifier_count,
        &recognized_modifier,
        &tone_seen
    );
    if (status != MK_OK) {
        return status;
    }
    if (!recognized_modifier || mk_streq(base, normalized)) {
        status = MK_ERR_UNKNOWN_GRAPHEME;
        goto finish;
    }

    status = mk_resolve_entry(system, base, &base_resolved);
    if (status == MK_OK) {
        for (i = 0; i < base_resolved.feature_count; i++) {
            status = mk_add_owned_feature(&features, &count, &cap, base_resolved.features[i]);
            if (status != MK_OK) {
                goto finish;
            }
        }
    } else {
        status = mk_lookup_normalized(system, base, &base_entry);
        if (status != MK_OK) {
            goto finish;
        }
        status = mk_copy_entry_features(base_entry, &features, &count, &cap);
        if (status != MK_OK) {
            goto finish;
        }
    }
    if (system->builtin->kind == MK_SYSTEM_VALUED) {
        status = mk_apply_valued_modifiers(features, count, modifiers, modifier_count);
        if (status != MK_OK) {
            goto finish;
        }
    } else {
        for (i = 0; i < modifier_count; i++) {
            status = mk_add_owned_feature(&features, &count, &cap, modifiers[i]);
            if (status != MK_OK) {
                goto finish;
            }
        }
    }
    if (tone_seen &&
        !mk_feature_array_marks_nucleus(features, count) &&
        mk_feature_array_marks_sonorant(features, count)) {
        status = mk_add_owned_feature(&features, &count, &cap, "syllabic");
        if (status != MK_OK) {
            goto finish;
        }
    }
    if (tone_seen && !mk_feature_array_marks_nucleus(features, count)) {
        status = MK_ERR_UNKNOWN_GRAPHEME;
        goto finish;
    }

finish:
    free(base);
    mk_resolved_entry_clear(&base_resolved);
    mk_free_owned_feature_array(modifiers, modifier_count);
    if (status != MK_OK) {
        mk_free_owned_feature_array(features, count);
        return status;
    }
    out->owned_grapheme = mk_strdup_internal(normalized);
    if (out->owned_grapheme == NULL) {
        mk_free_owned_feature_array(features, count);
        return MK_ERR_OOM;
    }
    out->grapheme = out->owned_grapheme;
    out->owned_features = features;
    out->owned_feature_count = count;
    out->features = (const char *const *)features;
    out->feature_count = count;
    return MK_OK;
}

mk_status mk_resolve_entry(
    const mk_system *system,
    const char *utf8_grapheme,
    mk_resolved_entry *out
)
{
    char *normalized;
    const mk_builtin_entry *entry = NULL;
    mk_status status;

    if (system == NULL || system->builtin == NULL || utf8_grapheme == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));

    status = mk_normalize_input_grapheme(utf8_grapheme, &normalized);
    if (status != MK_OK) {
        return status;
    }

    status = mk_lookup_normalized(system, normalized, &entry);
    if (status == MK_OK) {
        out->grapheme = entry->grapheme;
        out->features = entry->features;
        out->feature_count = entry->feature_count;
        mk_free_string(normalized);
        return MK_OK;
    }
    if (status == MK_ERR_UNKNOWN_GRAPHEME) {
        status = mk_synthesize_vowel_cluster(system, normalized, out);
        if (status == MK_ERR_UNKNOWN_GRAPHEME) {
            status = mk_synthesize_from_diacritics(system, normalized, out);
        }
        if (status == MK_ERR_UNKNOWN_GRAPHEME) {
            status = mk_synthesize_descriptive_complex(system, normalized, out);
        }
        if (status == MK_ERR_UNKNOWN_GRAPHEME) {
            status = mk_synthesize_descriptive_consonant_cluster(system, normalized, out);
        }
    }
    mk_free_string(normalized);
    return status;
}

mk_status mk_system_is_segment(
    const mk_system *system,
    const char *utf8_grapheme,
    int *out
)
{
    mk_resolved_entry entry;
    mk_status status;

    if (out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = 0;
    status = mk_resolve_entry(system, utf8_grapheme, &entry);
    if (status == MK_OK) {
        mk_resolved_entry_clear(&entry);
        *out = 1;
        return MK_OK;
    }
    if (status == MK_ERR_UNKNOWN_GRAPHEME) {
        return MK_OK;
    }
    return status;
}

mk_status mk_system_grapheme_features(
    const mk_system *system,
    const char *utf8_grapheme,
    mk_feature_set **out
)
{
    mk_resolved_entry entry;
    mk_status status;

    if (out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    status = mk_resolve_entry(system, utf8_grapheme, &entry);
    if (status != MK_OK) {
        return status;
    }
    status = mk_feature_set_from_borrowed(entry.features, entry.feature_count, out);
    mk_resolved_entry_clear(&entry);
    return status;
}

mk_status mk_system_segment_distance(
    const mk_system *system,
    const char *utf8_a,
    const char *utf8_b,
    double *out
)
{
    return mk_system_segment_distance_with_weights(system, utf8_a, utf8_b, NULL, out);
}

static double mk_min_double(double a, double b)
{
    return a < b ? a : b;
}

static double mk_component_distance(
    const mk_system *system,
    const char *a_text,
    const mk_resolved_entry *b_entry,
    const char *node_weights
)
{
    mk_resolved_entry a_resolved;
    mk_builtin_entry a;
    mk_builtin_entry b;
    double distance;

    memset(&a_resolved, 0, sizeof(a_resolved));
    if (mk_resolve_entry(system, a_text, &a_resolved) != MK_OK) {
        return 1.0;
    }
    a.grapheme = a_resolved.grapheme;
    a.features = a_resolved.features;
    a.feature_count = a_resolved.feature_count;
    b.grapheme = b_entry->grapheme;
    b.features = b_entry->features;
    b.feature_count = b_entry->feature_count;
    distance = mk_categorical_distance(system->builtin, &a, &b, node_weights);
    mk_resolved_entry_clear(&a_resolved);
    return isnan(distance) ? 1.0 : distance;
}

static double mk_cluster_component_distance(
    const mk_system *system,
    const char *a_text,
    const char *b_text,
    const char *node_weights
)
{
    mk_resolved_entry a_resolved;
    double distance;

    memset(&a_resolved, 0, sizeof(a_resolved));
    if (mk_resolve_entry(system, a_text, &a_resolved) != MK_OK) {
        return 1.0;
    }
    distance = mk_component_distance(system, b_text, &a_resolved, node_weights);
    mk_resolved_entry_clear(&a_resolved);
    return distance;
}

static double mk_distance_cluster_to_segment(
    const mk_system *system,
    const mk_resolved_entry *cluster,
    const mk_resolved_entry *segment,
    const char *node_weights
)
{
    double score;
    size_t i;

    if (cluster->cluster_component_count == 0) {
        return 1.0;
    }
    score = 0.7 * mk_component_distance(system, cluster->cluster_components[0], segment, node_weights);
    if (cluster->cluster_component_count > 1) {
        double rest = 0.0;
        for (i = 1; i < cluster->cluster_component_count; i++) {
            rest += mk_component_distance(system, cluster->cluster_components[i], segment, node_weights);
        }
        rest /= (double)(cluster->cluster_component_count - 1);
        score += 0.3 * rest;
    }
    score += 0.15 * (double)(cluster->cluster_component_count - 1);
    return mk_min_double(score, 1.0);
}

static double mk_vowel_cluster_distance(
    const mk_system *system,
    const mk_resolved_entry *a,
    const mk_resolved_entry *b,
    const char *node_weights
)
{
    mk_builtin_entry a_entry;
    mk_builtin_entry b_entry;
    double component_score = 0.0;
    double tone_score;
    double score;
    size_t i;

    if (mk_streq(a->grapheme, b->grapheme)) {
        return 0.0;
    }
    if (a->cluster_component_count == 0 && b->cluster_component_count == 0) {
        return 1.0;
    }
    if (a->cluster_component_count > 0 && b->cluster_component_count == 0) {
        component_score = mk_distance_cluster_to_segment(system, a, b, node_weights);
    } else if (a->cluster_component_count == 0 && b->cluster_component_count > 0) {
        component_score = mk_distance_cluster_to_segment(system, b, a, node_weights);
    } else {
        size_t common = a->cluster_component_count < b->cluster_component_count ?
            a->cluster_component_count : b->cluster_component_count;
        for (i = 0; i < common; i++) {
            component_score += mk_cluster_component_distance(
                system,
                a->cluster_components[i],
                b->cluster_components[i],
                node_weights
            );
        }
        component_score = common > 0 ? component_score / (double)common : 1.0;
        if (a->cluster_component_count > common) {
            component_score += 0.15 * (double)(a->cluster_component_count - common);
        }
        if (b->cluster_component_count > common) {
            component_score += 0.15 * (double)(b->cluster_component_count - common);
        }
        component_score = mk_min_double(component_score, 1.0);
    }

    a_entry.grapheme = a->grapheme;
    a_entry.features = a->features;
    a_entry.feature_count = a->feature_count;
    b_entry.grapheme = b->grapheme;
    b_entry.features = b->features;
    b_entry.feature_count = b->feature_count;
    tone_score = mk_categorical_distance(system->builtin, &a_entry, &b_entry, node_weights);
    if (isnan(tone_score)) {
        tone_score = 0.0;
    }
    score = 0.8 * component_score + 0.2 * tone_score;
    return mk_min_double(score, 1.0);
}

mk_status mk_system_segment_distance_with_weights(
    const mk_system *system,
    const char *utf8_a,
    const char *utf8_b,
    const char *node_weights,
    double *out
)
{
    mk_resolved_entry resolved_a;
    mk_resolved_entry resolved_b;
    mk_builtin_entry a;
    mk_builtin_entry b;
    mk_status status;

    if (out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = 0.0;

    status = mk_resolve_entry(system, utf8_a, &resolved_a);
    if (status != MK_OK) {
        return status;
    }
    status = mk_resolve_entry(system, utf8_b, &resolved_b);
    if (status != MK_OK) {
        mk_resolved_entry_clear(&resolved_a);
        return status;
    }

    a.grapheme = resolved_a.grapheme;
    a.features = resolved_a.features;
    a.feature_count = resolved_a.feature_count;
    b.grapheme = resolved_b.grapheme;
    b.features = resolved_b.features;
    b.feature_count = resolved_b.feature_count;

    if (system->builtin->kind == MK_SYSTEM_CATEGORICAL) {
        if (resolved_a.cluster_component_count > 0 || resolved_b.cluster_component_count > 0) {
            *out = mk_vowel_cluster_distance(system, &resolved_a, &resolved_b, node_weights);
        } else {
            *out = mk_categorical_distance(system->builtin, &a, &b, node_weights);
        }
    } else if (system->builtin->kind == MK_SYSTEM_VALUED) {
        *out = mk_valued_distance(system->builtin, &a, &b, node_weights);
    } else {
        mk_resolved_entry_clear(&resolved_a);
        mk_resolved_entry_clear(&resolved_b);
        return MK_ERR_UNSUPPORTED_MODEL;
    }
    mk_resolved_entry_clear(&resolved_a);
    mk_resolved_entry_clear(&resolved_b);
    if (isnan(*out)) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    return MK_OK;
}
