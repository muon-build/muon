/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_EVAL_H
#define MUON_LANG_EVAL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "lang/types.h"

struct workspace;
struct source;

enum eval_mode {
	eval_mode_default,
	eval_mode_repl,
	eval_mode_first,
};

enum eval_project_file_flags {
	eval_project_file_flag_first = 1 << 0,
};

bool eval_project(struct workspace *wk,
	const char *subproject_name,
	const char *cwd,
	const char *build_dir,
	uint32_t *proj_id);
bool eval_project_file(struct workspace *wk, const char *path, enum build_language lang, enum eval_project_file_flags flags);
bool eval(struct workspace *wk, struct source *src, enum build_language lang, enum eval_mode mode, obj *res);
bool eval_str(struct workspace *wk, const char *str, enum eval_mode mode, obj *res);
bool eval_str_label(struct workspace *wk, const char *label, const char *str, enum eval_mode mode, obj *res);
void repl(struct workspace *wk, bool dbg);

const char *determine_project_root(struct workspace *wk, const char *path);
const char *determine_build_file(struct workspace *wk, const char *cwd, enum build_language *out_lang);
#endif
