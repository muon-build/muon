#ifndef MUON_BACKEND_COMMON_ARGS_H
#define MUON_BACKEND_COMMON_ARGS_H

#include "lang/workspace.h"

bool setup_compiler_args(struct workspace *wk, const struct obj *tgt,
	const struct project *proj, obj include_dirs, obj dep_args,
	obj *joined_args);

struct setup_linker_args_ctx {
	enum linker_type linker;
	enum compiler_language link_lang;
	struct dep_args_ctx *args;
	obj implicit_deps;
};

void setup_linker_args(struct workspace *wk, const struct project *proj,
	const struct obj *tgt, struct setup_linker_args_ctx *ctx);

struct setup_compiler_args_includes_ctx {
	obj args;
	enum compiler_type t;
};
enum iteration_result setup_compiler_args_includes(struct workspace *wk, void *_ctx, obj v);
#endif
