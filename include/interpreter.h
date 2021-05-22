#ifndef BOSON_INTERPRETER_H
#define BOSON_INTERPRETER_H

#include <stdbool.h>
#include <stddef.h>

#include "lexer.h"
#include "parser.h"
#include "workspace.h"
#include "object.h"

bool interpret(struct ast *ast, struct workspace *wk);
bool interp_node(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj);
bool typecheck(struct obj *o, enum obj_type type);
#endif
