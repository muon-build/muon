#ifndef MUON_BACKEND_COMMON_ARGS_H
#define MUON_BACKEND_COMMON_ARGS_H

#include "lang/workspace.h"

bool setup_compiler_args(struct workspace *wk, const struct obj *tgt,
	const struct project *proj, obj include_dirs, obj args_dict);
void push_linker_args_link_with(struct workspace *wk, enum linker_type linker,
	obj link_args, obj link_with);
#endif
