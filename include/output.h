#ifndef MUON_OUTPUT_H
#define MUON_OUTPUT_H

#include "workspace.h"

struct outpath {
	const char *private_dir, *setup, *tests;
};

const extern struct outpath outpath;

bool output_build(struct workspace *wk);
bool output_private_file(struct workspace *wk, char dest[PATH_MAX],
	const char *path, const char *txt);
#endif
