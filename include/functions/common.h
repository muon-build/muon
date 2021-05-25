#ifndef FUNCTIONS_COMMON_H
#define FUNCTIONS_COMMON_H

#include "parser.h"
#include "workspace.h"

typedef bool (*func_impl)(struct workspace *wk, uint32_t recvr, uint32_t args_node, uint32_t *obj);

struct func_impl_name {
	const char *name;
	func_impl func;
};

struct args_norm { enum obj_type type; uint32_t val, node; bool set; };
struct args_kw { const char *key; enum obj_type type; uint32_t val, node; bool set; };

#define ARG_TYPE_NULL 1000 // a number higher than any valid node type

bool todo(struct workspace *wk, uint32_t rcvr_id, uint32_t args_node, uint32_t *obj);
bool check_lang(struct workspace *wk, uint32_t n_id, uint32_t id);

bool interp_args(struct workspace *wk, uint32_t args_node,
	struct args_norm positional_args[],
	struct args_norm optional_positional_args[],
	struct args_kw keyword_args[]);
bool builtin_run(struct workspace *wk, uint32_t rcvr_id, uint32_t node_id, uint32_t *obj);
#endif
