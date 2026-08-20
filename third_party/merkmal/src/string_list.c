#include "internal.h"

#include <stdlib.h>
#include <string.h>

char *mk_strdup_internal(const char *s)
{
    size_t len;
    char *copy;

    if (s == NULL) {
        return NULL;
    }
    len = strlen(s);
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len + 1);
    return copy;
}

int mk_streq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

mk_status mk_string_list_from_borrowed(
    const char *const *items,
    size_t count,
    mk_string_list **out
)
{
    mk_string_list *list;
    size_t i;

    if (out == NULL || (items == NULL && count != 0)) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;

    list = (mk_string_list *)calloc(1, sizeof(*list));
    if (list == NULL) {
        return MK_ERR_OOM;
    }
    if (count != 0) {
        list->items = (char **)calloc(count, sizeof(*list->items));
        if (list->items == NULL) {
            free(list);
            return MK_ERR_OOM;
        }
    }
    list->count = count;

    for (i = 0; i < count; i++) {
        list->items[i] = mk_strdup_internal(items[i]);
        if (list->items[i] == NULL) {
            mk_string_list_free(list);
            return MK_ERR_OOM;
        }
    }

    *out = list;
    return MK_OK;
}

mk_status mk_string_list_new(
    const char *const *items,
    size_t count,
    mk_string_list **out
)
{
    return mk_string_list_from_borrowed(items, count, out);
}

size_t mk_string_list_size(const mk_string_list *list)
{
    return list == NULL ? 0 : list->count;
}

const char *mk_string_list_get(const mk_string_list *list, size_t index)
{
    if (list == NULL || index >= list->count) {
        return NULL;
    }
    return list->items[index];
}

void mk_string_list_free(mk_string_list *list)
{
    size_t i;

    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    free(list);
}

void mk_free_string(char *s)
{
    free(s);
}
