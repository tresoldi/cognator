#include "internal.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

typedef struct mk_node_group {
    const char *node;
    int touched;
    int differs;
} mk_node_group;

static int mk_features_contains(
    const char *const *features,
    size_t feature_count,
    const char *feature
)
{
    size_t i;

    for (i = 0; i < feature_count; i++) {
        if (mk_streq(features[i], feature)) {
            return 1;
        }
    }
    return 0;
}

static int mk_entry_contains(const mk_builtin_entry *entry, const char *feature)
{
    return mk_features_contains(entry->features, entry->feature_count, feature);
}

static int mk_is_leaf_feature(const char *feature)
{
    size_t i;

    for (i = 0; i < mk_clements_hume_leaf_count; i++) {
        const mk_geometry_leaf *leaf = &mk_clements_hume_leaves[i];
        if ((leaf->positive[0] != '\0' && mk_streq(leaf->positive, feature)) ||
            (leaf->negative[0] != '\0' && mk_streq(leaf->negative, feature))) {
            return 1;
        }
    }
    return 0;
}

static const char *mk_feature_node(const char *feature)
{
    size_t i;

    for (i = 0; i < mk_clements_hume_feature_to_node_count; i++) {
        if (mk_streq(mk_clements_hume_feature_to_node[i].feature, feature)) {
            return mk_clements_hume_feature_to_node[i].node;
        }
    }
    return NULL;
}

static const mk_feature_path *mk_find_feature_path(const char *feature)
{
    size_t i;

    for (i = 0; i < mk_clements_hume_feature_path_count; i++) {
        if (mk_streq(mk_clements_hume_feature_paths[i].feature, feature)) {
            return &mk_clements_hume_feature_paths[i];
        }
    }
    return NULL;
}

mk_status mk_feature_distance(
    const char *feature_a,
    const char *feature_b,
    int *out
)
{
    const mk_feature_path *path_a;
    const mk_feature_path *path_b;
    size_t common;
    size_t limit;

    if (feature_a == NULL || feature_b == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    if (mk_streq(feature_a, feature_b)) {
        *out = 0;
        return MK_OK;
    }

    path_a = mk_find_feature_path(feature_a);
    path_b = mk_find_feature_path(feature_b);
    if (path_a == NULL || path_b == NULL) {
        *out = 999;
        return MK_OK;
    }

    common = 0;
    limit = path_a->path_count < path_b->path_count ?
        path_a->path_count : path_b->path_count;
    while (common < limit && mk_streq(path_a->path[common], path_b->path[common])) {
        common++;
    }

    *out = (int)((path_a->path_count - common) + (path_b->path_count - common));
    return MK_OK;
}

static double mk_node_depth_value(const char *node)
{
    size_t i;

    for (i = 0; i < mk_clements_hume_node_depth_count; i++) {
        if (mk_streq(mk_clements_hume_node_depths[i].node, node)) {
            return mk_clements_hume_node_depths[i].depth;
        }
    }
    return 2.0;
}

static const char *mk_geometry_node_parent(const char *node)
{
    size_t i;

    for (i = 0; i < mk_clements_hume_node_parent_count; i++) {
        if (mk_streq(mk_clements_hume_node_parents[i].node, node)) {
            return mk_clements_hume_node_parents[i].parent;
        }
    }
    return "";
}

static const mk_node_weight_preset *mk_find_weight_preset(const char *name)
{
    size_t i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < mk_clements_hume_weight_preset_count; i++) {
        if (mk_streq(mk_clements_hume_weight_presets[i].name, name)) {
            return &mk_clements_hume_weight_presets[i];
        }
    }
    return NULL;
}

static mk_status mk_resolve_weight_preset(
    const char *node_weights,
    const mk_node_weight_preset **out
)
{
    if (out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (node_weights == NULL || node_weights[0] == '\0' || mk_streq(node_weights, "None")) {
        return MK_OK;
    }
    *out = mk_find_weight_preset(node_weights);
    return *out == NULL ? MK_ERR_INVALID_ARGUMENT : MK_OK;
}

static double mk_direct_node_weight(
    const mk_node_weight_preset *preset,
    const char *node
)
{
    size_t i;

    if (preset == NULL) {
        return 1.0;
    }
    for (i = 0; i < preset->weight_count; i++) {
        if (mk_streq(preset->weights[i].node, node)) {
            return preset->weights[i].weight;
        }
    }
    return 1.0;
}

static double mk_resolved_node_weight(
    const mk_node_weight_preset *preset,
    const char *node
)
{
    const char *current;
    double weight = 1.0;

    if (preset == NULL) {
        return 1.0;
    }
    current = node;
    while (current != NULL && current[0] != '\0') {
        weight *= mk_direct_node_weight(preset, current);
        current = mk_geometry_node_parent(current);
    }
    return weight;
}

static double mk_dimension_weight(
    const mk_node_weight_preset *preset,
    const char *node,
    double base_weight
)
{
    if (preset != NULL && preset->flat) {
        return 1.0;
    }
    return base_weight * mk_resolved_node_weight(preset, node);
}

static mk_node_group *mk_get_group(
    mk_node_group *groups,
    size_t *count,
    const char *node
)
{
    size_t i;

    for (i = 0; i < *count; i++) {
        if (mk_streq(groups[i].node, node)) {
            return &groups[i];
        }
    }
    groups[*count].node = node;
    groups[*count].touched = 1;
    groups[*count].differs = 0;
    (*count)++;
    return &groups[*count - 1];
}

static void mk_process_node_feature(
    const mk_builtin_entry *a,
    const mk_builtin_entry *b,
    const char *feature,
    mk_node_group *groups,
    size_t *group_count
)
{
    const char *node;
    mk_node_group *group;
    int in_a;
    int in_b;

    if (mk_is_leaf_feature(feature)) {
        return;
    }
    node = mk_feature_node(feature);
    if (node == NULL) {
        return;
    }

    in_a = mk_entry_contains(a, feature);
    in_b = mk_entry_contains(b, feature);
    group = mk_get_group(groups, group_count, node);
    if (in_a != in_b) {
        group->differs = 1;
    }
}

static double mk_dimension_value(
    const mk_builtin_entry *entry,
    const mk_scalar_dimension *dimension
)
{
    size_t i;

    for (i = 0; i < dimension->positive_count; i++) {
        if (mk_entry_contains(entry, dimension->positive[i])) {
            return 1.0;
        }
    }
    for (i = 0; i < dimension->negative_count; i++) {
        if (mk_entry_contains(entry, dimension->negative[i])) {
            return -1.0;
        }
    }
    return 0.0;
}

static double mk_scalar_categorical_distance(
    const mk_builtin_system *system,
    const mk_builtin_entry *a,
    const mk_builtin_entry *b,
    const mk_node_weight_preset *preset
)
{
    size_t i;
    double total_weight = 0.0;
    double total_diff = 0.0;

    if (a == b || mk_streq(a->grapheme, b->grapheme)) {
        return 0.0;
    }

    for (i = 0; i < system->scalar_dimension_count; i++) {
        const mk_scalar_dimension *dimension = &system->scalar_dimensions[i];
        double a_val = mk_dimension_value(a, dimension);
        double b_val = mk_dimension_value(b, dimension);
        double weight;
        double divisor;

        if (a_val == 0.0 && b_val == 0.0) {
            continue;
        }

        weight = mk_dimension_weight(preset, dimension->geometry_node, dimension->weight);
        total_weight += weight;
        divisor = dimension->negative_count == 0 ? 1.0 : 2.0;
        total_diff += weight * fabs(a_val - b_val) / divisor;
    }

    return total_weight > 0.0 ? total_diff / total_weight : 0.0;
}

static double mk_categorical_distance_resolved(
    const mk_builtin_system *system,
    const mk_builtin_entry *a,
    const mk_builtin_entry *b,
    const mk_node_weight_preset *preset
)
{
    double total_weight;
    double total_diff;
    size_t i;
    mk_node_group groups[128];
    size_t group_count;

    if (a == b || mk_streq(a->grapheme, b->grapheme)) {
        return 0.0;
    }
    if (system != NULL && system->scalar_dimension_count > 0) {
        return mk_scalar_categorical_distance(system, a, b, preset);
    }

    total_weight = 0.0;
    total_diff = 0.0;

    for (i = 0; i < mk_clements_hume_leaf_count; i++) {
        const mk_geometry_leaf *leaf = &mk_clements_hume_leaves[i];
        double weight = mk_dimension_weight(preset, leaf->parent, 1.0 / leaf->depth);
        int a_pos = leaf->positive[0] != '\0' && mk_entry_contains(a, leaf->positive);
        int a_neg = leaf->negative[0] != '\0' && mk_entry_contains(a, leaf->negative);
        int b_pos = leaf->positive[0] != '\0' && mk_entry_contains(b, leaf->positive);
        int b_neg = leaf->negative[0] != '\0' && mk_entry_contains(b, leaf->negative);
        double a_val;
        double b_val;
        double divisor;

        total_weight += weight;
        if (!a_pos && !a_neg && !b_pos && !b_neg) {
            total_weight -= weight;
            continue;
        }

        a_val = a_pos ? 1.0 : (a_neg ? -1.0 : 0.0);
        b_val = b_pos ? 1.0 : (b_neg ? -1.0 : 0.0);
        divisor = leaf->negative[0] == '\0' ? 1.0 : 2.0;
        total_diff += weight * fabs(a_val - b_val) / divisor;
    }

    memset(groups, 0, sizeof(groups));
    group_count = 0;
    for (i = 0; i < a->feature_count; i++) {
        mk_process_node_feature(a, b, a->features[i], groups, &group_count);
    }
    for (i = 0; i < b->feature_count; i++) {
        if (!mk_entry_contains(a, b->features[i])) {
            mk_process_node_feature(a, b, b->features[i], groups, &group_count);
        }
    }

    for (i = 0; i < group_count; i++) {
        double weight = mk_dimension_weight(
            preset,
            groups[i].node,
            1.0 / mk_node_depth_value(groups[i].node)
        );
        total_weight += weight;
        if (groups[i].differs) {
            total_diff += weight;
        }
    }

    return total_weight > 0.0 ? total_diff / total_weight : 0.0;
}

double mk_categorical_distance(
    const mk_builtin_system *system,
    const mk_builtin_entry *a,
    const mk_builtin_entry *b,
    const char *node_weights
)
{
    const mk_node_weight_preset *preset = NULL;

    if (mk_resolve_weight_preset(node_weights, &preset) != MK_OK) {
        return NAN;
    }
    return mk_categorical_distance_resolved(system, a, b, preset);
}

static int mk_label_value(
    const mk_builtin_entry *entry,
    const char *feature,
    double *out
)
{
    size_t i;
    size_t feature_len = strlen(feature);

    for (i = 0; i < entry->feature_count; i++) {
        const char *label = entry->features[i];
        if (strncmp(label, feature, feature_len) == 0 && label[feature_len] == '=') {
            char state = label[feature_len + 1];
            if (state == '.') {
                return 0;
            }
            if (state == '+') {
                *out = 1.0;
            } else if (state == '-') {
                *out = -1.0;
            } else {
                *out = 0.0;
            }
            return 1;
        }
    }
    return 0;
}

double mk_valued_distance(
    const mk_builtin_system *system,
    const mk_builtin_entry *a,
    const mk_builtin_entry *b,
    const char *node_weights
)
{
    size_t i;
    double total_weight = 0.0;
    double total_diff = 0.0;
    const mk_node_weight_preset *preset = NULL;

    if (mk_resolve_weight_preset(node_weights, &preset) != MK_OK) {
        return NAN;
    }

    if (a == b || mk_streq(a->grapheme, b->grapheme)) {
        return 0.0;
    }

    for (i = 0; i < system->geometry_map_count; i++) {
        double a_val = 0.0;
        double b_val = 0.0;
        int a_ok = mk_label_value(a, system->geometry_map[i].feature, &a_val);
        int b_ok = mk_label_value(b, system->geometry_map[i].feature, &b_val);
        double weight;

        if (!a_ok || !b_ok) {
            continue;
        }
        if (a_val == 0.0 && b_val == 0.0) {
            continue;
        }

        weight = system->dimension_weights != NULL ? system->dimension_weights[i] : 0.5;
        weight = mk_dimension_weight(preset, system->geometry_map[i].node, weight);
        total_weight += weight;
        total_diff += weight * fabs(a_val - b_val) / 2.0;
    }

    return total_weight > 0.0 ? total_diff / total_weight : 0.0;
}

mk_status mk_sound_distance(
    const char *const *features_a,
    size_t feature_a_count,
    const char *const *features_b,
    size_t feature_b_count,
    const char *node_weights,
    double *out
)
{
    mk_builtin_entry a;
    mk_builtin_entry b;
    const mk_node_weight_preset *preset = NULL;
    mk_status status;

    if (out == NULL ||
        (feature_a_count > 0 && features_a == NULL) ||
        (feature_b_count > 0 && features_b == NULL)) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    status = mk_resolve_weight_preset(node_weights, &preset);
    if (status != MK_OK) {
        return status;
    }

    a.grapheme = "__mk_features_a__";
    a.features = features_a;
    a.feature_count = feature_a_count;
    b.grapheme = "__mk_features_b__";
    b.features = features_b;
    b.feature_count = feature_b_count;
    *out = mk_categorical_distance_resolved(NULL, &a, &b, preset);
    if (isnan(*out)) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    return MK_OK;
}
