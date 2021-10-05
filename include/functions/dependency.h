#ifndef MUON_FUNCTIONS_DEPENDENCY_H
#define MUON_FUNCTIONS_DEPENDENCY_H
#include "functions/common.h"

struct dep_args_ctx {
	obj args_dict, include_dirs, link_with, link_args;
	bool relativize, recursive;
};

enum iteration_result dep_args_iter(struct workspace *wk, void *_ctx, obj val);
enum iteration_result dep_args_link_with_iter(struct workspace *wk, void *_ctx, uint32_t val_id);
enum iteration_result dep_args_includes_iter(struct workspace *wk, void *_ctx, obj inc_id);

void dep_args_ctx_init(struct workspace *wk, struct dep_args_ctx *ctx);
bool deps_args(struct workspace *wk, obj deps, struct dep_args_ctx *ctx);
bool dep_args(struct workspace *wk, obj dep, struct dep_args_ctx *ctx);

extern const struct func_impl_name impl_tbl_dependency[];
#endif
