#ifndef MUON_OUTPUT_H
#define MUON_OUTPUT_H

#include "workspace.h"

struct outpath {
	const char *private_dir, *setup, *tests;
};

const extern struct outpath outpath;

bool output_build(struct workspace *wk);
#endif
