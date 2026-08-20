#include "internal.h"

#include <stdlib.h>

mk_status mk_feature_set_from_borrowed(
    const char *const *items,
    size_t count,
    mk_feature_set **out
)
{
    mk_feature_set *set;
    size_t i;

    if (out == NULL || (items == NULL && count != 0)) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;

    set = (mk_feature_set *)calloc(1, sizeof(*set));
    if (set == NULL) {
        return MK_ERR_OOM;
    }
    if (count != 0) {
        set->items = (char **)calloc(count, sizeof(*set->items));
        if (set->items == NULL) {
            free(set);
            return MK_ERR_OOM;
        }
    }
    set->count = count;

    for (i = 0; i < count; i++) {
        set->items[i] = mk_strdup_internal(items[i]);
        if (set->items[i] == NULL) {
            mk_feature_set_free(set);
            return MK_ERR_OOM;
        }
    }

    *out = set;
    return MK_OK;
}

size_t mk_feature_set_size(const mk_feature_set *features)
{
    return features == NULL ? 0 : features->count;
}

const char *mk_feature_set_get(const mk_feature_set *features, size_t index)
{
    if (features == NULL || index >= features->count) {
        return NULL;
    }
    return features->items[index];
}

void mk_feature_set_free(mk_feature_set *features)
{
    size_t i;

    if (features == NULL) {
        return;
    }
    for (i = 0; i < features->count; i++) {
        free(features->items[i]);
    }
    free(features->items);
    free(features);
}
