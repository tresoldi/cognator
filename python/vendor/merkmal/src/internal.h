#ifndef MK_INTERNAL_H
#define MK_INTERNAL_H

#include "merkmal.h"

#include <stddef.h>

typedef enum mk_system_type {
    MK_SYSTEM_CATEGORICAL = 1,
    MK_SYSTEM_VALUED = 2,
    MK_SYSTEM_TRAINED = 3
} mk_system_type;

typedef struct mk_builtin_entry {
    const char *grapheme;
    const char *const *features;
    size_t feature_count;
} mk_builtin_entry;

typedef struct mk_resolved_entry {
    const char *grapheme;
    const char *const *features;
    size_t feature_count;
    char **owned_features;
    size_t owned_feature_count;
    char *owned_grapheme;
    char **cluster_components;
    size_t cluster_component_count;
} mk_resolved_entry;

typedef struct mk_geometry_leaf {
    const char *name;
    const char *positive;
    const char *negative;
    double depth;
    const char *parent;
} mk_geometry_leaf;

typedef struct mk_feature_node_map {
    const char *feature;
    const char *node;
} mk_feature_node_map;

typedef struct mk_node_depth {
    const char *node;
    double depth;
} mk_node_depth;

typedef struct mk_node_parent {
    const char *node;
    const char *parent;
} mk_node_parent;

typedef struct mk_node_weight {
    const char *node;
    double weight;
} mk_node_weight;

typedef struct mk_node_weight_preset {
    const char *name;
    const mk_node_weight *weights;
    size_t weight_count;
    int flat;
} mk_node_weight_preset;

typedef struct mk_diacritic_map {
    const char *mark;
    const char *feature;
} mk_diacritic_map;

typedef struct mk_tone_mark {
    const char *mark;
    const char *const *features;
    size_t feature_count;
} mk_tone_mark;

typedef struct mk_valued_diacritic_effect {
    const char *modifier;
    const char *const *alternatives;
    size_t alternative_count;
    char state;
} mk_valued_diacritic_effect;

typedef struct mk_feature_path {
    const char *feature;
    const char *const *path;
    size_t path_count;
} mk_feature_path;

typedef struct mk_scalar_dimension {
    const char *name;
    const char *geometry_node;
    const char *const *positive;
    size_t positive_count;
    const char *const *negative;
    size_t negative_count;
    double weight;
} mk_scalar_dimension;

typedef struct mk_builtin_system {
    const char *name;
    mk_system_type kind;
    const mk_builtin_entry *entries;
    size_t entry_count;
    const mk_feature_node_map *geometry_map;
    size_t geometry_map_count;
    const double *dimension_weights;
    const mk_scalar_dimension *scalar_dimensions;
    size_t scalar_dimension_count;
} mk_builtin_system;

struct mk_system {
    const mk_builtin_system *builtin;
    mk_builtin_system owned;
    int owns;
};

struct mk_registry {
    mk_system *systems;
    size_t system_count;
};

struct mk_string_list {
    char **items;
    size_t count;
};

struct mk_feature_set {
    char **items;
    size_t count;
};

extern const mk_builtin_system mk_builtin_systems[];
extern const size_t mk_builtin_system_count;
extern const mk_geometry_leaf mk_clements_hume_leaves[];
extern const size_t mk_clements_hume_leaf_count;
extern const mk_feature_node_map mk_clements_hume_feature_to_node[];
extern const size_t mk_clements_hume_feature_to_node_count;
extern const mk_node_depth mk_clements_hume_node_depths[];
extern const size_t mk_clements_hume_node_depth_count;
extern const mk_node_parent mk_clements_hume_node_parents[];
extern const size_t mk_clements_hume_node_parent_count;
extern const mk_node_weight_preset mk_clements_hume_weight_presets[];
extern const size_t mk_clements_hume_weight_preset_count;
extern const mk_feature_path mk_clements_hume_feature_paths[];
extern const size_t mk_clements_hume_feature_path_count;
extern const mk_diacritic_map mk_default_combining_diacritics[];
extern const size_t mk_default_combining_diacritic_count;
extern const mk_diacritic_map mk_default_suffix_diacritics[];
extern const size_t mk_default_suffix_diacritic_count;
extern const mk_diacritic_map mk_default_prefix_diacritics[];
extern const size_t mk_default_prefix_diacritic_count;
extern const mk_tone_mark mk_default_tone_marks[];
extern const size_t mk_default_tone_mark_count;
extern const mk_valued_diacritic_effect mk_default_valued_diacritic_effects[];
extern const size_t mk_default_valued_diacritic_effect_count;

char *mk_strdup_internal(const char *s);
mk_status mk_string_list_from_borrowed(
    const char *const *items,
    size_t count,
    mk_string_list **out
);
mk_status mk_feature_set_from_borrowed(
    const char *const *items,
    size_t count,
    mk_feature_set **out
);

mk_status mk_lookup_features(
    const mk_system *system,
    const char *utf8_grapheme,
    const mk_builtin_entry **out
);
mk_status mk_resolve_entry(
    const mk_system *system,
    const char *utf8_grapheme,
    mk_resolved_entry *out
);
void mk_resolved_entry_clear(mk_resolved_entry *entry);

mk_status mk_normalize_input_grapheme(
    const char *utf8_in,
    char **utf8_out
);
double mk_categorical_distance(
    const mk_builtin_system *system,
    const mk_builtin_entry *a,
    const mk_builtin_entry *b,
    const char *node_weights
);
double mk_valued_distance(
    const mk_builtin_system *system,
    const mk_builtin_entry *a,
    const mk_builtin_entry *b,
    const char *node_weights
);

int mk_streq(const char *a, const char *b);

#endif
