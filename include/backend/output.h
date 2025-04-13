/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_OUTPUT_H
#define MUON_BACKEND_OUTPUT_H

#include "lang/workspace.h"

struct output_path {
	const char *private_dir, *summary, *tests, *install, *compiler_check_cache, *option_info, *introspect_dir,
		*meson_private_dir, *debug_log;
	struct {
		const char *projectinfo;
		const char *targets;
		const char *benchmarks;
		const char *buildoptions;
		const char *buildsystem_files;
		const char *compilers;
		const char *dependencies;
		const char *scan_dependencies;
		const char *installed;
		const char *install_plan;
		const char *machines;
		const char *tests;
	} introspect_file;
};

extern const struct output_path output_path;

typedef bool((*with_open_callback)(struct workspace *wk, void *ctx, FILE *out));

FILE *output_open(const char *dir, const char *name);
bool with_open(const char *dir, const char *name, struct workspace *wk, void *ctx, with_open_callback cb);
#endif
