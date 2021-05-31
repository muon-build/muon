#ifndef BOSON_OPTS_H
#define BOSON_OPTS_H
#include "workspace.h"

struct setup_opts {
	const char *build;
};

bool opts_parse_setup(struct workspace *wk, struct setup_opts *opts, int argc, char *const argv[]);
#endif
