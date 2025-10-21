/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_OUTPUT_H
#define MUON_BACKEND_OUTPUT_H

#include "lang/workspace.h"

struct output_path_spec {
	const char *path;
	bool is_cache;
};

enum output_path_name {
	output_path_summary,
	output_path_tests,
	output_path_install,
	output_path_compiler_check_cache,
	output_path_option_info,
	output_path_debug_log,
	output_path_vsenv_cache,
	output_path_cmdline,
	output_path_name_count,
};

struct output_path {
	const char *private_dir;
	const char *introspect_dir;
	const char *meson_private_dir;
	struct output_path_spec paths[output_path_name_count];
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

FILE *output_open(struct workspace *wk, const char *dir, const char *name);
bool with_open(const char *dir, const char *name, struct workspace *wk, void *ctx, with_open_callback cb);
bool output_clear_caches(struct workspace *wk);
#endif
