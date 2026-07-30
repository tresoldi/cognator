#ifndef COGNATOR_INTERNAL_CONTEXT_H
#define COGNATOR_INTERNAL_CONTEXT_H

#include "cognator.h"
#include "merkmal.h"

struct cg_context {
    mk_registry *registry;
    const mk_system *system;
};

cg_status cg_from_mk_status(mk_status status);

#endif
