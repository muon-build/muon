#ifndef MUON_FUNCTIONS_COMMON_H
#define MUON_FUNCTIONS_COMMON_H

#include "lang/parser.h"
#include "lang/workspace.h"

typedef bool (*func_impl)(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res);
typedef obj (*func_impl_rcvr_transform)(struct workspace *wk, obj rcvr);

struct func_impl_name {
	const char *name;
	func_impl func;
	type_tag return_type;
	bool pure, fuzz_unsafe;
	func_impl_rcvr_transform rcvr_transform;
};

extern const struct func_impl_name *kernel_func_tbl[language_mode_count];
extern const struct func_impl_name *func_tbl[obj_type_count][language_mode_count];

struct args_norm { type_tag type; obj val, node; bool set; };
struct args_kw { const char *key; type_tag type; obj val, node; bool set; bool required; };

extern bool disabler_among_args_immunity, disable_fuzz_unsafe_functions;

void build_func_impl_tables(void);

const struct func_impl_name *func_lookup(const struct func_impl_name *impl_tbl, const char *name);

bool interp_args(struct workspace *wk, uint32_t args_node,
	struct args_norm positional_args[],
	struct args_norm optional_positional_args[],
	struct args_kw keyword_args[]);
bool builtin_run(struct workspace *wk, bool have_rcvr, obj rcvr_id, uint32_t node_id, obj *res);

bool analyze_function(struct workspace *wk, const struct func_impl_name *fi, uint32_t args_node, obj rcvr, obj *res);
#endif
