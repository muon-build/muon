#ifndef MUON_BACKEND_OUTPUT_H
#define MUON_BACKEND_OUTPUT_H

#include "lang/workspace.h"

struct output_path {
	const char *private_dir, *summary, *tests, *install,
		   *compiler_check_cache, *option_info;
};

extern const struct output_path output_path;

typedef bool ((with_open_callback)(struct workspace *wk, void *ctx, FILE *out));

FILE *output_open(const char *dir, const char *name);
bool with_open(const char *dir, const char *name, struct workspace *wk, void *ctx, with_open_callback cb);
#endif
