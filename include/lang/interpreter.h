#ifndef MUON_INTERPRETER_H
#define MUON_INTERPRETER_H

#include <stdbool.h>
#include <stddef.h>

#include "parser.h"
#include "workspace.h"
#include "object.h"

bool interp_node(struct workspace *wk, uint32_t n_id, uint32_t *obj_id);
void interp_error(struct workspace *wk, uint32_t n_id, const char *fmt, ...)
__attribute__ ((format(printf, 3, 4)));

bool typecheck_simple_err(struct workspace *wk, uint32_t obj_id, enum obj_type type);
bool typecheck(struct workspace *wk, uint32_t n_id, uint32_t obj_id, enum obj_type type);
bool boundscheck(struct workspace *wk, uint32_t n_id, uint32_t obj_id, int64_t *i);
#endif
