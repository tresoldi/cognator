#include "internal.h"

#include <stdlib.h>
#include <string.h>

#if MK_HAVE_UTF8PROC
#include <utf8proc.h>
#endif

static int mk_has_prefix(const char *s, const char *prefix)
{
    size_t n;

    if (s == NULL || prefix == NULL) {
        return 0;
    }
    n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

static mk_status mk_append_bytes(char **buf, size_t *len, size_t *cap, const char *s)
{
    size_t n;
    char *next;

    n = strlen(s);
    if (*len + n + 1 > *cap) {
        size_t new_cap = *cap == 0 ? 32 : *cap;
        while (*len + n + 1 > new_cap) {
            new_cap *= 2;
        }
        next = (char *)realloc(*buf, new_cap);
        if (next == NULL) {
            return MK_ERR_OOM;
        }
        *buf = next;
        *cap = new_cap;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
    return MK_OK;
}

static size_t mk_utf8_char_len(unsigned char c)
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

static unsigned int mk_utf8_codepoint(const char *p)
{
    unsigned char c = (unsigned char)p[0];

    if (c < 0x80) {
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        return ((unsigned int)(c & 0x1F) << 6) |
            (unsigned int)((unsigned char)p[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
        return ((unsigned int)(c & 0x0F) << 12) |
            ((unsigned int)((unsigned char)p[1] & 0x3F) << 6) |
            (unsigned int)((unsigned char)p[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0) {
        return ((unsigned int)(c & 0x07) << 18) |
            ((unsigned int)((unsigned char)p[1] & 0x3F) << 12) |
            ((unsigned int)((unsigned char)p[2] & 0x3F) << 6) |
            (unsigned int)((unsigned char)p[3] & 0x3F);
    }
    return c;
}

static int mk_is_combining_mark(const char *p)
{
    unsigned int cp = mk_utf8_codepoint(p);

    return (cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F);
}

static int mk_is_map_mark(const mk_diacritic_map *map, size_t count, const char *p)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (mk_has_prefix(p, map[i].mark)) {
            return 1;
        }
    }
    return 0;
}

static int mk_is_modifier_letter_or_symbol(const char *p)
{
    unsigned int cp = mk_utf8_codepoint(p);

    if (mk_is_map_mark(mk_default_prefix_diacritics, mk_default_prefix_diacritic_count, p) ||
        mk_is_map_mark(mk_default_suffix_diacritics, mk_default_suffix_diacritic_count, p)) {
        return 1;
    }
    return (cp >= 0x02B0 && cp <= 0x02FF) ||
        (cp >= 0x1D2C && cp <= 0x1D6A) ||
        (cp >= 0x1D9B && cp <= 0x1DBF) ||
        (cp >= 0x2070 && cp <= 0x209F);
}

static void mk_free_items(char **items, size_t count)
{
    size_t i;

    if (items == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

static const char *mk_resolve_slash(const char *s)
{
    const char *last;
    const char *p;

    last = NULL;
    for (p = s; *p != '\0'; p++) {
        if (*p == '/') {
            last = p;
        }
    }
    if (last != NULL && last[1] != '\0') {
        return last + 1;
    }
    return s;
}

static const char *mk_strip_leading_stress(const char *s)
{
    while (mk_has_prefix(s, "ˈ") || mk_has_prefix(s, "ˌ")) {
        s += 2;
    }
    return s;
}

mk_status mk_normalize_input_grapheme(
    const char *utf8_in,
    char **utf8_out
)
{
    const char *p;
    char *tmp;
    size_t len;
    size_t cap;
    mk_status status;

    if (utf8_in == NULL || utf8_out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *utf8_out = NULL;

    p = mk_strip_leading_stress(mk_resolve_slash(utf8_in));
    tmp = NULL;
    len = 0;
    cap = 0;

    while (*p != '\0') {
        if (mk_has_prefix(p, "ɡ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "g");
            p += strlen("ɡ");
        } else if (mk_has_prefix(p, "ʣ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "dz");
            p += strlen("ʣ");
        } else if (mk_has_prefix(p, "ʤ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "dʒ");
            p += strlen("ʤ");
        } else if (mk_has_prefix(p, "ʥ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "dʑ");
            p += strlen("ʥ");
        } else if (mk_has_prefix(p, "ʦ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ts");
            p += strlen("ʦ");
        } else if (mk_has_prefix(p, "ʧ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "tʃ");
            p += strlen("ʧ");
        } else if (mk_has_prefix(p, "ʨ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "tɕ");
            p += strlen("ʨ");
        } else if (mk_has_prefix(p, "ã")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ã");
            p += strlen("ã");
        } else if (mk_has_prefix(p, "ẽ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ẽ");
            p += strlen("ẽ");
        } else if (mk_has_prefix(p, "ĩ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ĩ");
            p += strlen("ĩ");
        } else if (mk_has_prefix(p, "õ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "õ");
            p += strlen("õ");
        } else if (mk_has_prefix(p, "ũ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ũ");
            p += strlen("ũ");
        } else if (mk_has_prefix(p, "ṽ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ṽ");
            p += strlen("ṽ");
        } else if (mk_has_prefix(p, "ñ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ñ");
            p += strlen("ñ");
        } else if (mk_has_prefix(p, "ỹ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ỹ");
            p += strlen("ỹ");
        } else if (mk_has_prefix(p, "ä")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ä");
            p += strlen("ä");
        } else if (mk_has_prefix(p, "ë")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ë");
            p += strlen("ë");
        } else if (mk_has_prefix(p, "ï")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ï");
            p += strlen("ï");
        } else if (mk_has_prefix(p, "ö")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ö");
            p += strlen("ö");
        } else if (mk_has_prefix(p, "ă")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ă");
            p += strlen("ă");
        } else if (mk_has_prefix(p, "ĕ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ĕ");
            p += strlen("ĕ");
        } else if (mk_has_prefix(p, "ĭ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ĭ");
            p += strlen("ĭ");
        } else if (mk_has_prefix(p, "ŏ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ŏ");
            p += strlen("ŏ");
        } else if (mk_has_prefix(p, "ŭ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ŭ");
            p += strlen("ŭ");
        } else if (mk_has_prefix(p, "ç")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ç");
            p += strlen("ç");
        } else if (mk_has_prefix(p, "á")) {
            status = mk_append_bytes(&tmp, &len, &cap, "á");
            p += strlen("á");
        } else if (mk_has_prefix(p, "é")) {
            status = mk_append_bytes(&tmp, &len, &cap, "é");
            p += strlen("é");
        } else if (mk_has_prefix(p, "í")) {
            status = mk_append_bytes(&tmp, &len, &cap, "í");
            p += strlen("í");
        } else if (mk_has_prefix(p, "ó")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ó");
            p += strlen("ó");
        } else if (mk_has_prefix(p, "ú")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ú");
            p += strlen("ú");
        } else if (mk_has_prefix(p, "ń")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ń");
            p += strlen("ń");
        } else if (mk_has_prefix(p, "à")) {
            status = mk_append_bytes(&tmp, &len, &cap, "à");
            p += strlen("à");
        } else if (mk_has_prefix(p, "è")) {
            status = mk_append_bytes(&tmp, &len, &cap, "è");
            p += strlen("è");
        } else if (mk_has_prefix(p, "ì")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ì");
            p += strlen("ì");
        } else if (mk_has_prefix(p, "ò")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ò");
            p += strlen("ò");
        } else if (mk_has_prefix(p, "ù")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ù");
            p += strlen("ù");
        } else if (mk_has_prefix(p, "â")) {
            status = mk_append_bytes(&tmp, &len, &cap, "â");
            p += strlen("â");
        } else if (mk_has_prefix(p, "ê")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ê");
            p += strlen("ê");
        } else if (mk_has_prefix(p, "î")) {
            status = mk_append_bytes(&tmp, &len, &cap, "î");
            p += strlen("î");
        } else if (mk_has_prefix(p, "ô")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ô");
            p += strlen("ô");
        } else if (mk_has_prefix(p, "û")) {
            status = mk_append_bytes(&tmp, &len, &cap, "û");
            p += strlen("û");
        } else if (mk_has_prefix(p, "ü")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ü");
            p += strlen("ü");
        } else if (mk_has_prefix(p, "ṳ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ṳ");
            p += strlen("ṳ");
        } else if (mk_has_prefix(p, "ḭ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ḭ");
            p += strlen("ḭ");
        } else if (mk_has_prefix(p, "ṵ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ṵ");
            p += strlen("ṵ");
        } else if (mk_has_prefix(p, "ÿ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ÿ");
            p += strlen("ÿ");
        } else if (mk_has_prefix(p, "ā")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ā");
            p += strlen("ā");
        } else if (mk_has_prefix(p, "ē")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ē");
            p += strlen("ē");
        } else if (mk_has_prefix(p, "ī")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ī");
            p += strlen("ī");
        } else if (mk_has_prefix(p, "ō")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ō");
            p += strlen("ō");
        } else if (mk_has_prefix(p, "ū")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ū");
            p += strlen("ū");
        } else if (mk_has_prefix(p, "'") || mk_has_prefix(p, "’")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ʼ");
            p += mk_has_prefix(p, "'") ? 1 : strlen("’");
        } else if (*p == ':') {
            status = mk_append_bytes(&tmp, &len, &cap, "ː");
            p++;
        } else {
            char one[5];
            size_t n = mk_utf8_char_len((unsigned char)*p);
            memcpy(one, p, n);
            one[n] = '\0';
            status = mk_append_bytes(&tmp, &len, &cap, one);
            p += n;
        }
        if (status != MK_OK) {
            free(tmp);
            return status;
        }
    }

    if (tmp == NULL) {
        tmp = mk_strdup_internal("");
        if (tmp == NULL) {
            return MK_ERR_OOM;
        }
    }

#if MK_HAVE_UTF8PROC
    {
        utf8proc_uint8_t *normalized = utf8proc_NFD((const utf8proc_uint8_t *)tmp);
        free(tmp);
        if (normalized == NULL) {
            return MK_ERR_OOM;
        }
        tmp = (char *)normalized;
    }
#endif

    *utf8_out = tmp;
    return MK_OK;
}

static const char *mk_compose_known_pair(const char *base, const char *mark)
{
    if (mk_streq(base, "a") && mk_streq(mark, "̃")) {
        return "ã";
    }
    if (mk_streq(base, "e") && mk_streq(mark, "̃")) {
        return "ẽ";
    }
    if (mk_streq(base, "i") && mk_streq(mark, "̃")) {
        return "ĩ";
    }
    if (mk_streq(base, "o") && mk_streq(mark, "̃")) {
        return "õ";
    }
    if (mk_streq(base, "u") && mk_streq(mark, "̃")) {
        return "ũ";
    }
    if (mk_streq(base, "v") && mk_streq(mark, "̃")) {
        return "ṽ";
    }
    if (mk_streq(base, "a") && mk_streq(mark, "̈")) {
        return "ä";
    }
    if (mk_streq(base, "e") && mk_streq(mark, "̈")) {
        return "ë";
    }
    if (mk_streq(base, "i") && mk_streq(mark, "̈")) {
        return "ï";
    }
    if (mk_streq(base, "o") && mk_streq(mark, "̈")) {
        return "ö";
    }
    if (mk_streq(base, "y") && mk_streq(mark, "̈")) {
        return "ÿ";
    }
    if (mk_streq(base, "a") && mk_streq(mark, "̆")) {
        return "ă";
    }
    if (mk_streq(base, "e") && mk_streq(mark, "̆")) {
        return "ĕ";
    }
    if (mk_streq(base, "i") && mk_streq(mark, "̆")) {
        return "ĭ";
    }
    if (mk_streq(base, "o") && mk_streq(mark, "̆")) {
        return "ŏ";
    }
    if (mk_streq(base, "u") && mk_streq(mark, "̆")) {
        return "ŭ";
    }
    if (mk_streq(base, "c") && mk_streq(mark, "̧")) {
        return "ç";
    }
    if (mk_streq(base, "a") && mk_streq(mark, "́")) {
        return "á";
    }
    if (mk_streq(base, "e") && mk_streq(mark, "́")) {
        return "é";
    }
    if (mk_streq(base, "i") && mk_streq(mark, "́")) {
        return "í";
    }
    if (mk_streq(base, "o") && mk_streq(mark, "́")) {
        return "ó";
    }
    if (mk_streq(base, "u") && mk_streq(mark, "́")) {
        return "ú";
    }
    if (mk_streq(base, "a") && mk_streq(mark, "̀")) {
        return "à";
    }
    if (mk_streq(base, "e") && mk_streq(mark, "̀")) {
        return "è";
    }
    if (mk_streq(base, "i") && mk_streq(mark, "̀")) {
        return "ì";
    }
    if (mk_streq(base, "o") && mk_streq(mark, "̀")) {
        return "ò";
    }
    if (mk_streq(base, "u") && mk_streq(mark, "̀")) {
        return "ù";
    }
    if (mk_streq(base, "a") && mk_streq(mark, "̂")) {
        return "â";
    }
    if (mk_streq(base, "e") && mk_streq(mark, "̂")) {
        return "ê";
    }
    if (mk_streq(base, "i") && mk_streq(mark, "̂")) {
        return "î";
    }
    if (mk_streq(base, "o") && mk_streq(mark, "̂")) {
        return "ô";
    }
    if (mk_streq(base, "u") && mk_streq(mark, "̂")) {
        return "û";
    }
    if (mk_streq(base, "u") && mk_streq(mark, "̈")) {
        return "ü";
    }
    if (mk_streq(base, "u") && mk_streq(mark, "̤")) {
        return "ṳ";
    }
    if (mk_streq(base, "i") && mk_streq(mark, "̰")) {
        return "ḭ";
    }
    if (mk_streq(base, "u") && mk_streq(mark, "̰")) {
        return "ṵ";
    }
    if (mk_streq(base, "a") && mk_streq(mark, "̄")) {
        return "ā";
    }
    if (mk_streq(base, "e") && mk_streq(mark, "̄")) {
        return "ē";
    }
    if (mk_streq(base, "i") && mk_streq(mark, "̄")) {
        return "ī";
    }
    if (mk_streq(base, "o") && mk_streq(mark, "̄")) {
        return "ō";
    }
    if (mk_streq(base, "u") && mk_streq(mark, "̄")) {
        return "ū";
    }
    return NULL;
}

mk_status mk_normalize_grapheme(
    const char *utf8_in,
    char **utf8_out
)
{
    const char *p;
    char *tmp;
    char *out;
    size_t len;
    size_t cap;
    mk_status status;

    if (utf8_in == NULL || utf8_out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *utf8_out = NULL;

    status = mk_normalize_input_grapheme(utf8_in, &tmp);
    if (status != MK_OK) {
        return status;
    }

    out = NULL;
    len = 0;
    cap = 0;
    p = tmp;
    while (*p != '\0') {
        size_t base_len = mk_utf8_char_len((unsigned char)*p);
        if (p[base_len] != '\0') {
            char base[5];
            char mark[5];
            size_t mark_len = mk_utf8_char_len((unsigned char)*(p + base_len));
            const char *composed;

            memcpy(base, p, base_len);
            base[base_len] = '\0';
            memcpy(mark, p + base_len, mark_len);
            mark[mark_len] = '\0';
            composed = mk_compose_known_pair(base, mark);
            if (composed != NULL) {
                status = mk_append_bytes(&out, &len, &cap, composed);
                p += base_len + mark_len;
            } else if (*p == 'g') {
                status = mk_append_bytes(&out, &len, &cap, "ɡ");
                p++;
            } else {
                char one[5];
                size_t n = mk_utf8_char_len((unsigned char)*p);
                memcpy(one, p, n);
                one[n] = '\0';
                status = mk_append_bytes(&out, &len, &cap, one);
                p += n;
            }
        } else if (*p == 'g') {
            status = mk_append_bytes(&out, &len, &cap, "ɡ");
            p++;
        } else {
            char one[5];
            memcpy(one, p, base_len);
            one[base_len] = '\0';
            status = mk_append_bytes(&out, &len, &cap, one);
            p += base_len;
        }
        if (status != MK_OK) {
            free(tmp);
            free(out);
            return status;
        }
    }
    free(tmp);

    if (out == NULL) {
        out = mk_strdup_internal("");
        if (out == NULL) {
            return MK_ERR_OOM;
        }
    }

#if MK_HAVE_UTF8PROC
    {
        utf8proc_uint8_t *nfc = utf8proc_NFC((const utf8proc_uint8_t *)out);
        free(out);
        if (nfc == NULL) {
            return MK_ERR_OOM;
        }
        out = (char *)nfc;
    }
#endif

    *utf8_out = out;
    return MK_OK;
}

static int mk_is_boundary(const char *p)
{
    return *p == '+' || *p == '.' || *p == '|' || mk_has_prefix(p, "‖");
}

static int mk_is_suffix_modifier(const char *p)
{
    return mk_is_modifier_letter_or_symbol(p);
}

static int mk_is_chao_digit(const char *p)
{
    return mk_has_prefix(p, "⁰") || mk_has_prefix(p, "¹") ||
        mk_has_prefix(p, "²") || mk_has_prefix(p, "³") ||
        mk_has_prefix(p, "⁴") || mk_has_prefix(p, "⁵");
}

static mk_status mk_segmentation_nfd(
    const char *utf8_in,
    char **utf8_out
)
{
    const char *p = utf8_in;
    char *tmp = NULL;
    size_t len = 0;
    size_t cap = 0;
    mk_status status;

    if (utf8_in == NULL || utf8_out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *utf8_out = NULL;

    while (*p != '\0') {
        if (mk_has_prefix(p, "ã")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ã");
            p += strlen("ã");
        } else if (mk_has_prefix(p, "ẽ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ẽ");
            p += strlen("ẽ");
        } else if (mk_has_prefix(p, "ĩ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ĩ");
            p += strlen("ĩ");
        } else if (mk_has_prefix(p, "õ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "õ");
            p += strlen("õ");
        } else if (mk_has_prefix(p, "ũ")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ũ");
            p += strlen("ũ");
        } else if (mk_has_prefix(p, "á")) {
            status = mk_append_bytes(&tmp, &len, &cap, "á");
            p += strlen("á");
        } else if (mk_has_prefix(p, "é")) {
            status = mk_append_bytes(&tmp, &len, &cap, "é");
            p += strlen("é");
        } else if (mk_has_prefix(p, "í")) {
            status = mk_append_bytes(&tmp, &len, &cap, "í");
            p += strlen("í");
        } else if (mk_has_prefix(p, "ó")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ó");
            p += strlen("ó");
        } else if (mk_has_prefix(p, "ú")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ú");
            p += strlen("ú");
        } else if (mk_has_prefix(p, "à")) {
            status = mk_append_bytes(&tmp, &len, &cap, "à");
            p += strlen("à");
        } else if (mk_has_prefix(p, "è")) {
            status = mk_append_bytes(&tmp, &len, &cap, "è");
            p += strlen("è");
        } else if (mk_has_prefix(p, "ì")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ì");
            p += strlen("ì");
        } else if (mk_has_prefix(p, "ò")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ò");
            p += strlen("ò");
        } else if (mk_has_prefix(p, "ù")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ù");
            p += strlen("ù");
        } else if (mk_has_prefix(p, "â")) {
            status = mk_append_bytes(&tmp, &len, &cap, "â");
            p += strlen("â");
        } else if (mk_has_prefix(p, "ê")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ê");
            p += strlen("ê");
        } else if (mk_has_prefix(p, "î")) {
            status = mk_append_bytes(&tmp, &len, &cap, "î");
            p += strlen("î");
        } else if (mk_has_prefix(p, "ô")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ô");
            p += strlen("ô");
        } else if (mk_has_prefix(p, "û")) {
            status = mk_append_bytes(&tmp, &len, &cap, "û");
            p += strlen("û");
        } else if (mk_has_prefix(p, "ü")) {
            status = mk_append_bytes(&tmp, &len, &cap, "ü");
            p += strlen("ü");
        } else {
            char one[5];
            size_t n = mk_utf8_char_len((unsigned char)*p);
            memcpy(one, p, n);
            one[n] = '\0';
            status = mk_append_bytes(&tmp, &len, &cap, one);
            p += n;
        }
        if (status != MK_OK) {
            free(tmp);
            return status;
        }
    }

#if MK_HAVE_UTF8PROC
    {
        utf8proc_uint8_t *normalized = utf8proc_NFD((const utf8proc_uint8_t *)tmp);
        free(tmp);
        if (normalized == NULL) {
            return MK_ERR_OOM;
        }
        tmp = (char *)normalized;
    }
#endif

    if (tmp == NULL) {
        tmp = mk_strdup_internal("");
        if (tmp == NULL) {
            return MK_ERR_OOM;
        }
    }
    *utf8_out = tmp;
    return MK_OK;
}

static int mk_is_chao_digit_token(const char *s)
{
    const char *p = s;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    while (*p != '\0') {
        if (!mk_is_chao_digit(p)) {
            return 0;
        }
        p += mk_utf8_char_len((unsigned char)*p);
    }
    return 1;
}

static int mk_chao_digit_value(const char *p)
{
    if (mk_has_prefix(p, "⁰")) {
        return 0;
    }
    if (mk_has_prefix(p, "¹")) {
        return 1;
    }
    if (mk_has_prefix(p, "²")) {
        return 2;
    }
    if (mk_has_prefix(p, "³")) {
        return 3;
    }
    if (mk_has_prefix(p, "⁴")) {
        return 4;
    }
    if (mk_has_prefix(p, "⁵")) {
        return 5;
    }
    return -1;
}

static int mk_chao_token_has_nonzero(const char *s)
{
    const char *p = s;

    while (*p != '\0') {
        if (mk_chao_digit_value(p) > 0) {
            return 1;
        }
        p += mk_utf8_char_len((unsigned char)*p);
    }
    return 0;
}

static int mk_segment_is_syllabic(const char *segment)
{
    const char *p = segment;

    while (*p != '\0') {
        if (mk_has_prefix(p, "̩")) {
            return 1;
        }
        if (mk_is_combining_mark(p)) {
            p += mk_utf8_char_len((unsigned char)*p);
            continue;
        }
        if (*p == 'a' || *p == 'e' || *p == 'i' || *p == 'o' || *p == 'u' ||
            mk_has_prefix(p, "y") || mk_has_prefix(p, "ɛ") ||
            mk_has_prefix(p, "ɔ") || mk_has_prefix(p, "ə") ||
            mk_has_prefix(p, "ɨ") || mk_has_prefix(p, "ʉ") ||
            mk_has_prefix(p, "ɯ") || mk_has_prefix(p, "ɵ") ||
            mk_has_prefix(p, "œ") || mk_has_prefix(p, "æ") ||
            mk_has_prefix(p, "ɐ") || mk_has_prefix(p, "ɑ") ||
            mk_has_prefix(p, "ʌ") || mk_has_prefix(p, "ɪ") ||
            mk_has_prefix(p, "ʊ") || mk_has_prefix(p, "ɤ") ||
            mk_has_prefix(p, "ø") || mk_has_prefix(p, "ɘ") ||
            mk_has_prefix(p, "ɜ") || mk_has_prefix(p, "ɞ") ||
            mk_has_prefix(p, "ɒ") || mk_has_prefix(p, "ɶ") ||
            mk_has_prefix(p, "ɿ") || mk_has_prefix(p, "ʅ")) {
            return 1;
        }
        p += mk_utf8_char_len((unsigned char)*p);
    }
    return 0;
}

static mk_status mk_concat_strings(const char *a, const char *b, char **out)
{
    size_t a_len;
    size_t b_len;
    char *result;

    if (a == NULL || b == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    a_len = strlen(a);
    b_len = strlen(b);
    result = (char *)malloc(a_len + b_len + 1);
    if (result == NULL) {
        return MK_ERR_OOM;
    }
    memcpy(result, a, a_len);
    memcpy(result + a_len, b, b_len);
    result[a_len + b_len] = '\0';
    *out = result;
    return MK_OK;
}

mk_status mk_segment_ipa(
    const char *utf8_in,
    mk_string_list **out
)
{
    const char *p;
    char **items;
    size_t count;
    size_t cap;
    char *normalized;
    mk_status status;
    char *current;
    size_t current_len;
    size_t current_cap;
    int has_base;
    int after_tie;

    if (utf8_in == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;

    status = mk_segmentation_nfd(utf8_in, &normalized);
    if (status != MK_OK) {
        return status;
    }

    items = NULL;
    count = 0;
    cap = 0;
    current = NULL;
    current_len = 0;
    current_cap = 0;
    has_base = 0;
    after_tie = 0;
    p = normalized;

    while (*p != '\0') {
        size_t n;

        if (*p == ' ') {
            if (current_len > 0) {
                if (count + 1 > cap) {
                    char **next;
                    size_t new_cap = cap == 0 ? 8 : cap * 2;
                    next = (char **)realloc(items, new_cap * sizeof(*items));
                    if (next == NULL) {
                        free(current);
                        free(normalized);
                        mk_free_items(items, count);
                        return MK_ERR_OOM;
                    }
                    items = next;
                    cap = new_cap;
                }
                items[count++] = current;
                current = NULL;
                current_len = 0;
                current_cap = 0;
            }
            has_base = 0;
            after_tie = 0;
            p++;
            continue;
        }

        if (mk_is_chao_digit(p) || mk_is_boundary(p)) {
            if (current_len > 0) {
                if (count + 1 > cap) {
                    char **next;
                    size_t new_cap = cap == 0 ? 8 : cap * 2;
                    next = (char **)realloc(items, new_cap * sizeof(*items));
                    if (next == NULL) {
                        free(current);
                        free(normalized);
                        mk_free_items(items, count);
                        return MK_ERR_OOM;
                    }
                    items = next;
                    cap = new_cap;
                }
                items[count++] = current;
                current = NULL;
                current_len = 0;
                current_cap = 0;
            }
            has_base = 0;
            after_tie = 0;
        }

        if (count + 1 > cap && (mk_is_chao_digit(p) || mk_is_boundary(p))) {
            char **next;
            size_t new_cap = cap == 0 ? 8 : cap * 2;
            next = (char **)realloc(items, new_cap * sizeof(*items));
            if (next == NULL) {
                free(current);
                free(normalized);
                mk_free_items(items, count);
                return MK_ERR_OOM;
            }
            items = next;
            cap = new_cap;
        }

        if (mk_is_chao_digit(p)) {
            const char *start = p;
            char *token;
            while (mk_is_chao_digit(p)) {
                p += mk_utf8_char_len((unsigned char)*p);
            }
            n = (size_t)(p - start);
            token = (char *)malloc(n + 1);
            if (token == NULL) {
                free(normalized);
                mk_free_items(items, count);
                return MK_ERR_OOM;
            }
            memcpy(token, start, n);
            token[n] = '\0';
            items[count++] = token;
            continue;
        } else if (mk_is_boundary(p)) {
            char one[5];
            char *token;
            n = mk_utf8_char_len((unsigned char)*p);
            token = (char *)malloc(n + 1);
            if (token == NULL) {
                free(normalized);
                mk_free_items(items, count);
                return MK_ERR_OOM;
            }
            memcpy(token, p, n);
            token[n] = '\0';
            memcpy(one, p, n);
            p += n;
            items[count++] = token;
            (void)one;
            continue;
        } else if (mk_has_prefix(p, "͡") || mk_has_prefix(p, "͜")) {
            n = mk_utf8_char_len((unsigned char)*p);
            status = mk_append_bytes(&current, &current_len, &current_cap, mk_has_prefix(p, "͡") ? "͡" : "͜");
            if (status != MK_OK) {
                free(current);
                free(normalized);
                mk_free_items(items, count);
                return status;
            }
            after_tie = 1;
            p += n;
            continue;
        } else if (mk_is_combining_mark(p)) {
            char one[5];
            n = mk_utf8_char_len((unsigned char)*p);
            memcpy(one, p, n);
            one[n] = '\0';
            status = mk_append_bytes(&current, &current_len, &current_cap, one);
            if (status != MK_OK) {
                free(current);
                free(normalized);
                mk_free_items(items, count);
                return status;
            }
            p += n;
            continue;
        } else if (mk_is_suffix_modifier(p)) {
            char one[5];
            n = mk_utf8_char_len((unsigned char)*p);
            memcpy(one, p, n);
            one[n] = '\0';
            status = mk_append_bytes(&current, &current_len, &current_cap, one);
            if (status != MK_OK) {
                free(current);
                free(normalized);
                mk_free_items(items, count);
                return status;
            }
            p += n;
            continue;
        } else {
            char one[5];
            if (has_base && !after_tie && current_len > 0) {
                if (count + 1 > cap) {
                    char **next;
                    size_t new_cap = cap == 0 ? 8 : cap * 2;
                    next = (char **)realloc(items, new_cap * sizeof(*items));
                    if (next == NULL) {
                        free(current);
                        free(normalized);
                        mk_free_items(items, count);
                        return MK_ERR_OOM;
                    }
                    items = next;
                    cap = new_cap;
                }
                items[count++] = current;
                current = NULL;
                current_len = 0;
                current_cap = 0;
            }
            n = mk_utf8_char_len((unsigned char)*p);
            memcpy(one, p, n);
            one[n] = '\0';
            status = mk_append_bytes(&current, &current_len, &current_cap, one);
            if (status != MK_OK) {
                free(current);
                free(normalized);
                mk_free_items(items, count);
                return status;
            }
            has_base = 1;
            after_tie = 0;
            p += n;
            continue;
        }
    }

    if (current_len > 0) {
        if (count + 1 > cap) {
            char **next;
            size_t new_cap = cap == 0 ? 8 : cap * 2;
            next = (char **)realloc(items, new_cap * sizeof(*items));
            if (next == NULL) {
                free(current);
                free(normalized);
                mk_free_items(items, count);
                return MK_ERR_OOM;
            }
            items = next;
            cap = new_cap;
        }
        items[count++] = current;
        current = NULL;
    }
    free(normalized);

    {
        mk_string_list *list = (mk_string_list *)calloc(1, sizeof(*list));
        if (list == NULL) {
            mk_free_items(items, count);
            return MK_ERR_OOM;
        }
        list->items = items;
        list->count = count;
        *out = list;
    }

    return MK_OK;
}

mk_status mk_merge_tone_digits(
    const mk_string_list *segments,
    mk_string_list **out
)
{
    char **items;
    size_t count;
    size_t i;

    if (segments == NULL || out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    items = NULL;
    count = 0;

    if (segments->count > 0) {
        items = (char **)calloc(segments->count, sizeof(*items));
        if (items == NULL) {
            return MK_ERR_OOM;
        }
    }

    for (i = 0; i < segments->count; i++) {
        const char *segment = segments->items[i];
        if (mk_is_chao_digit_token(segment)) {
            size_t j;

            if (!mk_chao_token_has_nonzero(segment)) {
                continue;
            }
            j = count;
            while (j > 0) {
                j--;
                if (mk_streq(items[j], "+")) {
                    break;
                }
                if (mk_segment_is_syllabic(items[j])) {
                    char *merged = NULL;
                    mk_status status = mk_concat_strings(items[j], segment, &merged);
                    if (status != MK_OK) {
                        mk_free_items(items, count);
                        return status;
                    }
                    free(items[j]);
                    items[j] = merged;
                    break;
                }
                if (j == 0) {
                    break;
                }
            }
            continue;
        }

        items[count] = mk_strdup_internal(segment);
        if (items[count] == NULL) {
            mk_free_items(items, count);
            return MK_ERR_OOM;
        }
        count++;
    }

    {
        mk_string_list *list = (mk_string_list *)calloc(1, sizeof(*list));
        if (list == NULL) {
            mk_free_items(items, count);
            return MK_ERR_OOM;
        }
        list->items = items;
        list->count = count;
        *out = list;
    }
    return MK_OK;
}

mk_status mk_segment_ipa_merged(
    const char *utf8_in,
    mk_string_list **out
)
{
    mk_string_list *segments = NULL;
    mk_status status;

    if (out == NULL) {
        return MK_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    status = mk_segment_ipa(utf8_in, &segments);
    if (status != MK_OK) {
        return status;
    }
    status = mk_merge_tone_digits(segments, out);
    mk_string_list_free(segments);
    return status;
}
