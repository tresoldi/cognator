#ifndef MERKMAL_H
#define MERKMAL_H

#include <stddef.h>

#if defined(_WIN32) && defined(MERKMAL_SHARED)
#  if defined(MERKMAL_BUILDING_LIBRARY)
#    define MK_API __declspec(dllexport)
#  else
#    define MK_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define MK_API __attribute__((visibility("default")))
#else
#  define MK_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mk_registry mk_registry;
typedef struct mk_system mk_system;
typedef struct mk_string_list mk_string_list;
typedef struct mk_feature_set mk_feature_set;

typedef enum mk_status {
    MK_OK = 0,
    MK_ERR_INVALID_ARGUMENT,
    MK_ERR_UNKNOWN_SYSTEM,
    MK_ERR_UNKNOWN_GRAPHEME,
    MK_ERR_UNSUPPORTED_MODEL,
    MK_ERR_PARSE,
    MK_ERR_OOM
} mk_status;

MK_API const char *mk_status_string(mk_status status);

MK_API mk_status mk_registry_new_builtin(mk_registry **out);
MK_API void mk_registry_free(mk_registry *registry);

MK_API mk_status mk_registry_list_systems(
    const mk_registry *registry,
    mk_string_list **out
);

MK_API mk_status mk_registry_get_system(
    const mk_registry *registry,
    const char *name,
    const mk_system **out
);

MK_API mk_status mk_registry_add_model_text(
    mk_registry *registry,
    const char *model_text
);

MK_API mk_status mk_system_name(const mk_system *system, const char **out);
MK_API mk_status mk_system_kind(const mk_system *system, const char **out);

MK_API mk_status mk_system_is_segment(
    const mk_system *system,
    const char *utf8_grapheme,
    int *out
);

MK_API mk_status mk_system_grapheme_features(
    const mk_system *system,
    const char *utf8_grapheme,
    mk_feature_set **out
);

MK_API mk_status mk_system_segment_distance(
    const mk_system *system,
    const char *utf8_a,
    const char *utf8_b,
    double *out
);

MK_API mk_status mk_system_segment_distance_with_weights(
    const mk_system *system,
    const char *utf8_a,
    const char *utf8_b,
    const char *node_weights,
    double *out
);

MK_API mk_status mk_feature_distance(
    const char *feature_a,
    const char *feature_b,
    int *out
);

MK_API mk_status mk_sound_distance(
    const char *const *features_a,
    size_t feature_a_count,
    const char *const *features_b,
    size_t feature_b_count,
    const char *node_weights,
    double *out
);

MK_API mk_status mk_normalize_grapheme(
    const char *utf8_in,
    char **utf8_out
);

MK_API mk_status mk_segment_ipa(
    const char *utf8_in,
    mk_string_list **out
);

MK_API mk_status mk_merge_tone_digits(
    const mk_string_list *segments,
    mk_string_list **out
);

MK_API mk_status mk_segment_ipa_merged(
    const char *utf8_in,
    mk_string_list **out
);

MK_API mk_status mk_string_list_new(
    const char *const *items,
    size_t count,
    mk_string_list **out
);
MK_API size_t mk_string_list_size(const mk_string_list *list);
MK_API const char *mk_string_list_get(const mk_string_list *list, size_t index);
MK_API void mk_string_list_free(mk_string_list *list);

MK_API size_t mk_feature_set_size(const mk_feature_set *features);
MK_API const char *mk_feature_set_get(const mk_feature_set *features, size_t index);
MK_API void mk_feature_set_free(mk_feature_set *features);

MK_API void mk_free_string(char *s);

#ifdef __cplusplus
}
#endif

#endif
