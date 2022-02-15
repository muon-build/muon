#ifndef MUON_FUNCTIONS_COMMON_H
#define MUON_FUNCTIONS_COMMON_H

#include "lang/parser.h"
#include "lang/workspace.h"

typedef bool (*func_impl)(struct workspace *wk, uint32_t recvr, uint32_t args_node, obj *res);

struct func_impl_name {
	const char *name;
	func_impl func;
};

struct args_norm { enum obj_type type; obj val, node; bool set; };
struct args_kw { const char *key; enum obj_type type; obj val, node; bool set; bool required; };

extern bool disabler_among_args_immunity;

bool todo(struct workspace *wk, obj rcvr_id, uint32_t args_node, obj *res);
bool func_lookup(const struct func_impl_name *impl_tbl, const char *name, func_impl *res);

bool interp_args(struct workspace *wk, uint32_t args_node,
	struct args_norm positional_args[],
	struct args_norm optional_positional_args[],
	struct args_kw keyword_args[]);
bool builtin_run(struct workspace *wk, bool have_rcvr, obj rcvr_id, uint32_t node_id, obj *res);
#endif
