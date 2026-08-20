#include "internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void mk_free_owned_system(mk_system *system)
{
    size_t i;

    if (system == NULL || !system->owns) {
        return;
    }
    free((char *)system->owned.name);
    for (i = 0; i < system->owned.entry_count; i++) {
        size_t j;
        mk_builtin_entry *entry = (mk_builtin_entry *)&system->owned.entries[i];
        char **features = (char **)entry->features;

        free((char *)entry->grapheme);
        for (j = 0; j < entry->feature_count; j++) {
            free(features[j]);
        }
        free(features);
    }
    free((mk_builtin_entry *)system->owned.entries);
    system->builtin = NULL;
    memset(&system->owned, 0, sizeof(system->owned));
    system->owns = 0;
}

mk_status mk_registry_new_builtin(mk_registry **out)
{
    mk_registry *registry;
    size_t i;

    if (out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;

    registry = (mk_registry *)calloc(1, sizeof(*registry));
    if (registry == NULL) {
        return MK_ERR_OOM;
    }

    registry->systems = (mk_system *)calloc(
        mk_builtin_system_count,
        sizeof(*registry->systems)
    );
    if (registry->systems == NULL) {
        free(registry);
        return MK_ERR_OOM;
    }
    registry->system_count = mk_builtin_system_count;

    for (i = 0; i < mk_builtin_system_count; i++) {
        registry->systems[i].builtin = &mk_builtin_systems[i];
    }

    *out = registry;
    return MK_OK;
}

void mk_registry_free(mk_registry *registry)
{
    size_t i;

    if (registry == NULL) {
        return;
    }
    for (i = 0; i < registry->system_count; i++) {
        mk_free_owned_system(&registry->systems[i]);
    }
    free(registry->systems);
    free(registry);
}

mk_status mk_registry_list_systems(
    const mk_registry *registry,
    mk_string_list **out
)
{
    const char **names;
    size_t i;
    mk_status status;

    if (registry == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }

    names = (const char **)calloc(registry->system_count, sizeof(*names));
    if (names == NULL) {
        return MK_ERR_OOM;
    }
    for (i = 0; i < registry->system_count; i++) {
        names[i] = registry->systems[i].builtin->name;
    }

    status = mk_string_list_from_borrowed(names, registry->system_count, out);
    free(names);
    return status;
}

mk_status mk_registry_get_system(
    const mk_registry *registry,
    const char *name,
    const mk_system **out
)
{
    size_t i;

    if (registry == NULL || name == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;

    for (i = 0; i < registry->system_count; i++) {
        if (mk_streq(registry->systems[i].builtin->name, name)) {
            *out = &registry->systems[i];
            return MK_OK;
        }
    }

    return MK_ERR_UNKNOWN_SYSTEM;
}

static char *mk_trim(char *line)
{
    char *end;

    while (*line != '\0' && isspace((unsigned char)*line)) {
        line++;
    }
    end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return line;
}

static char *mk_next_token(char **cursor)
{
    char *start;

    while (**cursor != '\0' && isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }
    if (**cursor == '\0') {
        return NULL;
    }
    start = *cursor;
    while (**cursor != '\0' && !isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }
    if (**cursor != '\0') {
        **cursor = '\0';
        (*cursor)++;
    }
    return start;
}

static void mk_free_entries(mk_builtin_entry *entries, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        size_t j;
        char **features = (char **)entries[i].features;
        free((char *)entries[i].grapheme);
        for (j = 0; j < entries[i].feature_count; j++) {
            free(features[j]);
        }
        free(features);
    }
    free(entries);
}

static void mk_free_feature_array(char **features, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        free(features[i]);
    }
    free(features);
}

static mk_status mk_add_parsed_entry(
    mk_builtin_entry **entries,
    size_t *count,
    size_t *cap,
    char *cursor
)
{
    char *grapheme;
    char **features = NULL;
    size_t feature_count = 0;
    size_t feature_cap = 0;
    mk_builtin_entry *next_entries;

    grapheme = mk_next_token(&cursor);
    if (grapheme == NULL) {
        return MK_ERR_PARSE;
    }

    while (1) {
        char *feature = mk_next_token(&cursor);
        char **next_features;
        if (feature == NULL) {
            break;
        }
        if (feature_count + 1 > feature_cap) {
            size_t new_cap = feature_cap == 0 ? 8 : feature_cap * 2;
            next_features = (char **)realloc(features, new_cap * sizeof(*features));
            if (next_features == NULL) {
                mk_free_feature_array(features, feature_count);
                return MK_ERR_OOM;
            }
            features = next_features;
            feature_cap = new_cap;
        }
        features[feature_count] = mk_strdup_internal(feature);
        if (features[feature_count] == NULL) {
            mk_free_feature_array(features, feature_count);
            return MK_ERR_OOM;
        }
        feature_count++;
    }
    if (feature_count == 0) {
        free(features);
        return MK_ERR_PARSE;
    }

    if (*count + 1 > *cap) {
        size_t new_cap = *cap == 0 ? 8 : *cap * 2;
        next_entries = (mk_builtin_entry *)realloc(*entries, new_cap * sizeof(**entries));
        if (next_entries == NULL) {
            mk_free_feature_array(features, feature_count);
            return MK_ERR_OOM;
        }
        *entries = next_entries;
        *cap = new_cap;
    }

    (*entries)[*count].grapheme = mk_strdup_internal(grapheme);
    if ((*entries)[*count].grapheme == NULL) {
        mk_free_feature_array(features, feature_count);
        return MK_ERR_OOM;
    }
    (*entries)[*count].features = (const char *const *)features;
    (*entries)[*count].feature_count = feature_count;
    (*count)++;
    return MK_OK;
}

mk_status mk_registry_add_model_text(
    mk_registry *registry,
    const char *model_text
)
{
    char *copy;
    char *line;
    char *name = NULL;
    int saw_categorical = 0;
    mk_builtin_entry *entries = NULL;
    size_t entry_count = 0;
    size_t entry_cap = 0;
    mk_system *next_systems;
    mk_system *slot;

    if (registry == NULL || model_text == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }

    copy = mk_strdup_internal(model_text);
    if (copy == NULL) {
        return MK_ERR_OOM;
    }

    line = copy;
    while (line != NULL) {
        char *next = strchr(line, '\n');
        char *trimmed;

        if (next != NULL) {
            *next = '\0';
            next++;
        }
        trimmed = mk_trim(line);
        if (trimmed[0] != '\0' && trimmed[0] != '#') {
            if (strncmp(trimmed, "@model", 6) == 0 && isspace((unsigned char)trimmed[6])) {
                char *cursor = trimmed + 6;
                char *token = mk_next_token(&cursor);
                free(name);
                name = token == NULL ? NULL : mk_strdup_internal(token);
                if (token != NULL && name == NULL) {
                    free(copy);
                    mk_free_entries(entries, entry_count);
                    return MK_ERR_OOM;
                }
            } else if (strncmp(trimmed, "@type", 5) == 0 && isspace((unsigned char)trimmed[5])) {
                char *cursor = trimmed + 5;
                char *token = mk_next_token(&cursor);
                saw_categorical = token != NULL && strcmp(token, "categorical") == 0;
                if (!saw_categorical) {
                    free(copy);
                    free(name);
                    mk_free_entries(entries, entry_count);
                    return MK_ERR_UNSUPPORTED_MODEL;
                }
            } else if (strncmp(trimmed, "grapheme", 8) == 0 && isspace((unsigned char)trimmed[8])) {
                char *cursor = trimmed + 8;
                mk_status status = mk_add_parsed_entry(&entries, &entry_count, &entry_cap, cursor);
                if (status != MK_OK) {
                    free(copy);
                    free(name);
                    mk_free_entries(entries, entry_count);
                    return status;
                }
            }
        }
        line = next;
    }
    free(copy);

    if (name == NULL || !saw_categorical || entry_count == 0) {
        free(name);
        mk_free_entries(entries, entry_count);
        return MK_ERR_PARSE;
    }

    next_systems = (mk_system *)realloc(
        registry->systems,
        (registry->system_count + 1) * sizeof(*registry->systems)
    );
    if (next_systems == NULL) {
        free(name);
        mk_free_entries(entries, entry_count);
        return MK_ERR_OOM;
    }
    registry->systems = next_systems;
    slot = &registry->systems[registry->system_count];
    memset(slot, 0, sizeof(*slot));
    slot->owned.name = name;
    slot->owned.kind = MK_SYSTEM_CATEGORICAL;
    slot->owned.entries = entries;
    slot->owned.entry_count = entry_count;
    slot->builtin = &slot->owned;
    slot->owns = 1;
    registry->system_count++;
    return MK_OK;
}
