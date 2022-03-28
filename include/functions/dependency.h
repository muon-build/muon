#ifndef MUON_FUNCTIONS_DEPENDENCY_H
#define MUON_FUNCTIONS_DEPENDENCY_H
#include "functions/common.h"

struct dep_args_ctx {
	obj compile_args,
	    include_dirs,
	    link_with,
	    link_with_not_found,
	    link_args,
	    order_deps,
	    rpath;
	bool relativize, recursive;
	enum dep_flags parts;
	enum include_type include_type;
};

void dep_args_ctx_init(struct workspace *wk, struct dep_args_ctx *ctx);
bool deps_args_link_with_only(struct workspace *wk, obj link_with, struct dep_args_ctx *ctx);
bool deps_args(struct workspace *wk, obj deps, struct dep_args_ctx *ctx);
bool dep_args(struct workspace *wk, obj dep, struct dep_args_ctx *ctx);

extern const struct func_impl_name impl_tbl_dependency[];
#endif
