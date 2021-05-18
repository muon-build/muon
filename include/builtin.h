#ifndef BOSON_BUILTIN_H
#define BOSON_BUILTIN_H

#include <stdbool.h>

#include "parser.h"
#include "interpreter.h"

bool builtin_run(struct ast *ast, struct workspace *wk, struct obj *recvr, struct node *n, uint32_t *obj);
#endif
