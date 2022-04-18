#ifndef MUON_FUNCTIONS_COMMON_H
#define MUON_FUNCTIONS_COMMON_H

#include "lang/parser.h"
#include "lang/workspace.h"

typedef bool (*func_impl)(struct workspace *wk, uint32_t recvr, uint32_t args_node, obj *res);

struct func_impl_name {
	const char *name;
	func_impl func;
	uint32_t return_type;
};

extern const struct func_impl_name *func_tbl[obj_type_count][language_mode_count];

struct args_norm { uint32_t type; obj val, node; bool set; };
struct args_kw { const char *key; uint32_t type; obj val, node; bool set; bool required; };

extern bool disabler_among_args_immunity;

bool todo(struct workspace *wk, obj rcvr_id, uint32_t args_node, obj *res);
const struct func_impl_name *func_lookup(const struct func_impl_name *impl_tbl, const char *name);

bool interp_args(struct workspace *wk, uint32_t args_node,
	struct args_norm positional_args[],
	struct args_norm optional_positional_args[],
	struct args_kw keyword_args[]);
bool analyze_function_args(struct workspace *wk, func_impl func, uint32_t args_node);
bool builtin_run(struct workspace *wk, bool have_rcvr, obj rcvr_id, uint32_t node_id, obj *res);
#endif
