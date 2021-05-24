#ifndef BOSON_INTERPRETER_H
#define BOSON_INTERPRETER_H

#include <stdbool.h>
#include <stddef.h>

#include "lexer.h"
#include "parser.h"
#include "workspace.h"
#include "object.h"

bool interpreter_interpret(struct workspace *wk);
bool interp_node(struct workspace *wk, uint32_t n_id, uint32_t *obj_id);
void interp_error(struct workspace *wk, uint32_t n_id, const char *fmt, ...)
__attribute__ ((format(printf, 3, 4)));

bool typecheck(struct workspace *wk, uint32_t n_id, uint32_t obj_id, enum obj_type type);
#endif
