#ifndef FUNCTIONS_COMMON_H
#define FUNCTIONS_COMMON_H

#include "parser.h"
#include "workspace.h"

typedef bool (*func_impl)(struct ast *ast, struct workspace *wk, uint32_t recvr, struct node *n, uint32_t *obj);

struct func_impl_name {
	const char *name;
	func_impl func;
};

struct args_norm { enum obj_type type; uint32_t val; bool set; };
struct args_kw { const char *key; enum obj_type type; uint32_t val; bool set; };

#define ARG_TYPE_NULL 1000 // a number higher than any valid node type

bool todo(struct ast *ast, struct workspace *wk, uint32_t rcvr_id, struct node *args, uint32_t *obj);
bool check_lang(struct workspace *wk, uint32_t id);

bool interp_args(struct ast *ast, struct workspace *wk, struct node *args,
	struct args_norm _an[], struct args_norm ao[], struct args_kw akw[]);
bool builtin_run(struct ast *ast, struct workspace *wk, uint32_t rcvr_id, struct node *n, uint32_t *obj);
#endif
