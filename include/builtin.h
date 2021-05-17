#ifndef BOSON_BUILTIN_H
#define BOSON_BUILTIN_H

#include <stdbool.h>

#include "parser.h"
#include "interpreter.h"

typedef bool (*builtin_func)(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj);
bool builtin_run(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj);
#endif
