#ifndef MUON_BACKEND_NINJA_H
#define MUON_BACKEND_NINJA_H

#include "lang/workspace.h"

struct write_tgt_ctx {
	FILE *out;
	const struct project *proj;
	bool wrote_default;
};

bool ninja_write_all(struct workspace *wk);
int ninja_run(const char *argstr, const char *chdir, const char *capture);
#endif
