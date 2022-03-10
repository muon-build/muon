#ifndef MUON_LANG_INTERPRETER_H
#define MUON_LANG_INTERPRETER_H

#include <stdbool.h>
#include <stddef.h>

#include "parser.h"
#include "workspace.h"
#include "object.h"

bool interp_node(struct workspace *wk, uint32_t n_id, obj *res);
void interp_error(struct workspace *wk, uint32_t n_id, const char *fmt, ...)
__attribute__ ((format(printf, 3, 4)));

bool typecheck_simple_err(struct workspace *wk, obj o, enum obj_type type);
bool typecheck_array(struct workspace *wk, uint32_t n_id, obj arr, enum obj_type type);
bool typecheck_dict(struct workspace *wk, uint32_t n_id, obj dict, enum obj_type type);
bool typecheck(struct workspace *wk, uint32_t n_id, obj obj_id, enum obj_type type);
bool boundscheck(struct workspace *wk, uint32_t n_id, uint32_t len, int64_t *i);
bool bounds_adjust(struct workspace *wk, uint32_t len, int64_t *i);
bool rangecheck(struct workspace *wk, uint32_t n_id, int64_t min, int64_t max, int64_t n);
#endif
