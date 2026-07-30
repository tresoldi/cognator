#include "internal/context.h"
#include "internal/data.h"

#include <stdlib.h>
#include <string.h>

static cg_status tokens_push(cg_tokens *tokens, cg_token token) {
    cg_token *next;

    if (tokens->count == tokens->cap) {
        size_t next_cap = tokens->cap == 0 ? 16 : tokens->cap * 2;
        next = (cg_token *)realloc(tokens->items, next_cap * sizeof(*next));
        if (next == NULL) {
            return CG_ERR_OOM;
        }
        tokens->items = next;
        tokens->cap = next_cap;
    }
    tokens->items[tokens->count++] = token;
    return CG_OK;
}

static int is_tone_shape(const char *grapheme) {
    const unsigned char *p = (const unsigned char *)grapheme;

    if (grapheme == NULL || grapheme[0] == '\0') {
        return 0;
    }
    while (*p != '\0') {
        if (*p >= '0' && *p <= '9') {
            p++;
            continue;
        }
        if ((p[0] == 0xCB && p[1] >= 0xA5 && p[1] <= 0xA9) ||
            (p[0] == 0xC2 && (p[1] == 0xB9 || p[1] == 0xB2 || p[1] == 0xB3)) ||
            (p[0] == 0xE2 && p[1] == 0x81 && (p[2] == 0xB0 || p[2] >= 0xB4))) {
            p += (p[0] == 0xE2) ? 3 : 2;
            continue;
        }
        return 0;
    }
    return 1;
}

static int last_nucleus_index(cg_tokens *tokens, size_t *index_out) {
    size_t i;

    for (i = tokens->count; i > 0; i--) {
        cg_token *token = &tokens->items[i - 1];
        if (token->boundary) {
            return 0;
        }
        if (token->role == CG_ROLE_V || token->role == CG_ROLE_R) {
            *index_out = i - 1;
            return 1;
        }
    }
    return 0;
}

static cg_status append_tone(cg_token *token, const char *tone) {
    size_t old_len = token->tone == NULL ? 0 : strlen(token->tone);
    size_t tone_len = strlen(tone);
    char *next = (char *)realloc((char *)token->tone, old_len + tone_len + 1);

    if (next == NULL) {
        return CG_ERR_OOM;
    }
    memcpy(next + old_len, tone, tone_len + 1);
    token->tone = next;
    return CG_OK;
}

static cg_status normalize_or_copy(const char *field, char **out) {
    mk_status status;

    *out = NULL;
    status = mk_normalize_grapheme(field, out);
    if (status == MK_OK) {
        return CG_OK;
    }
    if (status == MK_ERR_UNKNOWN_GRAPHEME || status == MK_ERR_INVALID_ARGUMENT) {
        *out = cg_strdup(field);
        return *out == NULL ? CG_ERR_OOM : CG_OK;
    }
    return cg_from_mk_status(status);
}

static unsigned long read_utf8(const unsigned char **p) {
    unsigned long cp;

    if ((*p)[0] < 0x80) {
        return *((*p)++);
    }
    if (((*p)[0] & 0xE0) == 0xC0) {
        cp = (unsigned long)(((*p)[0] & 0x1F) << 6) | (unsigned long)((*p)[1] & 0x3F);
        *p += 2;
        return cp;
    }
    if (((*p)[0] & 0xF0) == 0xE0) {
        cp = (unsigned long)(((*p)[0] & 0x0F) << 12) |
            (unsigned long)(((*p)[1] & 0x3F) << 6) |
            (unsigned long)((*p)[2] & 0x3F);
        *p += 3;
        return cp;
    }
    if (((*p)[0] & 0xF8) == 0xF0) {
        cp = (unsigned long)(((*p)[0] & 0x07) << 18) |
            (unsigned long)(((*p)[1] & 0x3F) << 12) |
            (unsigned long)(((*p)[2] & 0x3F) << 6) |
            (unsigned long)((*p)[3] & 0x3F);
        *p += 4;
        return cp;
    }
    return *((*p)++);
}

static int append_utf8(char **buf, size_t *len, size_t *cap, unsigned long cp) {
    unsigned char tmp[4];
    size_t n = 0;
    char *next;

    if (cp < 0x80) {
        tmp[n++] = (unsigned char)cp;
    } else if (cp < 0x800) {
        tmp[n++] = (unsigned char)(0xC0 | (cp >> 6));
        tmp[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        tmp[n++] = (unsigned char)(0xE0 | (cp >> 12));
        tmp[n++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    } else {
        tmp[n++] = (unsigned char)(0xF0 | (cp >> 18));
        tmp[n++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[n++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    }
    if (*len + n + 1 > *cap) {
        size_t next_cap = *cap == 0 ? 16 : *cap * 2;
        while (*len + n + 1 > next_cap) {
            next_cap *= 2;
        }
        next = (char *)realloc(*buf, next_cap);
        if (next == NULL) {
            return 0;
        }
        *buf = next;
        *cap = next_cap;
    }
    memcpy(*buf + *len, tmp, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 1;
}

static int is_reduced_away(unsigned long cp) {
    if (cp >= 0x0300 && cp <= 0x036F) {
        return 1;
    }
    if (cp >= 0x02B0 && cp <= 0x02FF) {
        return 1;
    }
    if (cp == 0x00B2 || cp == 0x00B3 || cp == 0x00B9) {
        return 1;
    }
    if (cp >= 0x2070 && cp <= 0x209F) {
        return 1;
    }
    return 0;
}

static unsigned long reduce_precomposed_vowel(unsigned long cp) {
    switch (cp) {
    case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
    case 0x0100: case 0x0102: case 0x01CD:
        return 'A';
    case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
    case 0x0101: case 0x0103: case 0x01CE:
        return 'a';
    case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: case 0x0112: case 0x0114:
    case 0x011A:
        return 'E';
    case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: case 0x0113: case 0x0115:
    case 0x011B:
        return 'e';
    case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: case 0x0128: case 0x012A:
    case 0x012C: case 0x01CF:
        return 'I';
    case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: case 0x0129: case 0x012B:
    case 0x012D: case 0x01D0:
        return 'i';
    case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x014C:
    case 0x014E: case 0x01D1:
        return 'O';
    case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x014D:
    case 0x014F: case 0x01D2:
        return 'o';
    case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: case 0x0168: case 0x016A:
    case 0x016C: case 0x01D3:
        return 'U';
    case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: case 0x0169: case 0x016B:
    case 0x016D: case 0x01D4:
        return 'u';
    default:
        return cp;
    }
}

cg_status cg_class_reduce(const char *grapheme, char **out) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    const unsigned char *p;

    if (grapheme == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    p = (const unsigned char *)grapheme;
    while (*p != '\0') {
        unsigned long cp = read_utf8(&p);
        if (is_reduced_away(cp)) {
            continue;
        }
        cp = reduce_precomposed_vowel(cp);
        if (!append_utf8(&buf, &len, &cap, cp)) {
            free(buf);
            return CG_ERR_OOM;
        }
    }
    if (buf == NULL || buf[0] == '\0') {
        free(buf);
        buf = cg_strdup(grapheme);
        if (buf == NULL) {
            return CG_ERR_OOM;
        }
    }
    *out = buf;
    return CG_OK;
}

cg_status cg_segment_parse_with_oov(
    const cg_context *ctx,
    const char *raw,
    cg_oov_report *oov_report,
    cg_tokens **out
) {
    char *copy;
    char *field;
    char *save = NULL;
    cg_tokens *tokens;

    if (ctx == NULL || raw == NULL || out == NULL) {
        return CG_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    tokens = (cg_tokens *)calloc(1, sizeof(*tokens));
    if (tokens == NULL) {
        return CG_ERR_OOM;
    }
    copy = cg_strdup(raw);
    if (copy == NULL) {
        cg_tokens_free(tokens);
        return CG_ERR_OOM;
    }
    for (field = strtok_r(copy, " \t\r\n", &save); field != NULL; field = strtok_r(NULL, " \t\r\n", &save)) {
        cg_token token = {0};
        cg_status status;
        int is_segment = 0;
        char *normalized = NULL;

        if (strcmp(field, "-") == 0) {
            continue;
        }
        if (strcmp(field, "+") == 0) {
            token.boundary = 1;
            token.grapheme = cg_strdup("");
            token.tone = cg_strdup("");
            if (token.grapheme == NULL || token.tone == NULL) {
                cg_free_token(&token);
                free(copy);
                cg_tokens_free(tokens);
                return CG_ERR_OOM;
            }
            status = tokens_push(tokens, token);
            if (status != CG_OK) {
                cg_free_token(&token);
                free(copy);
                cg_tokens_free(tokens);
                return status;
            }
            continue;
        }
        status = normalize_or_copy(field, &normalized);
        if (status != CG_OK) {
            free(copy);
            cg_tokens_free(tokens);
            return status;
        }
        if (is_tone_shape(normalized)) {
            size_t idx = 0;
            if (last_nucleus_index(tokens, &idx)) {
                status = append_tone(&tokens->items[idx], normalized);
                mk_free_string(normalized);
                if (status != CG_OK) {
                    free(copy);
                    cg_tokens_free(tokens);
                    return status;
                }
                continue;
            }
            token.role = CG_ROLE_T;
        } else {
            status = cg_context_is_segment(ctx, normalized, &is_segment);
            if (status != CG_OK) {
                mk_free_string(normalized);
                free(copy);
                cg_tokens_free(tokens);
                return status;
            }
            token.oov = is_segment ? 0 : 1;
            if (is_segment) {
                status = cg_context_grapheme_role(ctx, normalized, &token.role);
                if (status != CG_OK) {
                    mk_free_string(normalized);
                    free(copy);
                    cg_tokens_free(tokens);
                    return status;
                }
            } else {
                token.role = CG_ROLE_UNKNOWN;
                if (oov_report != NULL) {
                    status = cg_oov_report_add(oov_report, normalized);
                    if (status != CG_OK) {
                        mk_free_string(normalized);
                        free(copy);
                        cg_tokens_free(tokens);
                        return status;
                    }
                }
            }
        }
        token.grapheme = cg_strdup(normalized);
        token.tone = cg_strdup("");
        mk_free_string(normalized);
        if (token.grapheme == NULL || token.tone == NULL) {
            cg_free_token(&token);
            free(copy);
            cg_tokens_free(tokens);
            return CG_ERR_OOM;
        }
        status = tokens_push(tokens, token);
        if (status != CG_OK) {
            cg_free_token(&token);
            free(copy);
            cg_tokens_free(tokens);
            return status;
        }
    }
    free(copy);
    *out = tokens;
    return CG_OK;
}

cg_status cg_segment_parse(
    const cg_context *ctx,
    const char *raw,
    cg_tokens **out
) {
    return cg_segment_parse_with_oov(ctx, raw, NULL, out);
}

void cg_tokens_free(cg_tokens *tokens) {
    size_t i;

    if (tokens == NULL) {
        return;
    }
    for (i = 0; i < tokens->count; i++) {
        cg_free_token(&tokens->items[i]);
    }
    free(tokens->items);
    free(tokens);
}

uint64_t cg_tokens_count(const cg_tokens *tokens) {
    return tokens == NULL ? 0 : (uint64_t)tokens->count;
}

const cg_token *cg_tokens_get(const cg_tokens *tokens, uint64_t index) {
    if (tokens == NULL || index >= tokens->count) {
        return NULL;
    }
    return &tokens->items[index];
}
