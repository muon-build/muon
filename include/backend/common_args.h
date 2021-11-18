#ifndef MUON_BACKEND_COMMON_ARGS_H
#define MUON_BACKEND_COMMON_ARGS_H

#include "lang/workspace.h"

bool setup_compiler_args(struct workspace *wk, const struct obj *tgt,
	const struct project *proj, obj include_dirs, obj args_dict);
void setup_linker_args(struct workspace *wk, const struct project *proj,
	enum linker_type linker, enum compiler_language link_language,
	obj rpaths, obj link_args, obj link_with);
#endif
