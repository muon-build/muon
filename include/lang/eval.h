#ifndef MUON_LANG_EVAL_H
#define MUON_LANG_EVAL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "lang/types.h"

struct workspace;
struct source;
struct darr;

enum language_mode {
	language_external,
	language_internal,
	language_opts,
	language_mode_count,
};

enum eval_mode {
	eval_mode_default,
	eval_mode_repl,
};

struct source_data {
	char *data;
	uint64_t data_len;
};

void source_data_destroy(struct source_data *sdata);
bool eval_project(struct workspace *wk, const char *subproject_name, const char *cwd,
	const char *build_dir, uint32_t *proj_id);
bool eval_project_file(struct workspace *wk, const char *path);
bool eval(struct workspace *wk, struct source *src, enum eval_mode mode, obj *res);
bool eval_str(struct workspace *wk, const char *str, enum eval_mode mode, obj *res);
void repl(struct workspace *wk, bool dbg);
#endif
