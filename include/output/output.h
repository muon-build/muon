#ifndef MUON_OUTPUT_H
#define MUON_OUTPUT_H

#include "lang/workspace.h"

struct outpath {
	const char *private_dir, *setup, *tests;
};

extern const struct outpath outpath;

bool output_build(struct workspace *wk);
#endif
