#ifndef BOSON_OPTS_H
#define BOSON_OPTS_H
#include "workspace.h"

struct setup_opts {
	const char *build;
};

struct exe_opts {
	const char *capture;
	char *const *cmd;
};

bool opts_parse_setup(struct workspace *wk, struct setup_opts *opts,
	uint32_t argc, uint32_t argi, char *const argv[]);
bool opts_parse_exe(struct exe_opts *opts, uint32_t argc, uint32_t argi,
	char *const argv[]);
#endif
