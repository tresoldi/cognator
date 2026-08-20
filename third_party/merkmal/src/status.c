#include "merkmal.h"

const char *mk_status_string(mk_status status)
{
    switch (status) {
    case MK_OK:
        return "ok";
    case MK_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case MK_ERR_UNKNOWN_SYSTEM:
        return "unknown system";
    case MK_ERR_UNKNOWN_GRAPHEME:
        return "unknown grapheme";
    case MK_ERR_UNSUPPORTED_MODEL:
        return "unsupported model";
    case MK_ERR_PARSE:
        return "parse error";
    case MK_ERR_OOM:
        return "out of memory";
    default:
        return "unknown status";
    }
}
