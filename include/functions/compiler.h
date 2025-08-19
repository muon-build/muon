/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_COMPILER_H
#define MUON_FUNCTIONS_COMPILER_H

#include "lang/func_lookup.h"
#include "platform/run_cmd.h"


enum find_library_flag {
	find_library_flag_only_static = 1 << 0,
	find_library_flag_prefer_static = 1 << 0,
};

enum find_library_found_location {
	find_library_found_location_system_dirs,
	find_library_found_location_extra_dirs,
	find_library_found_location_link_arg,
};

struct find_library_result {
	obj found;
	enum find_library_found_location location;
};

#define COMPILER_DYNAMIC_LIB_EXTS ".so", ".dylib", ".dll.a", ".dll"
#define COMPILER_STATIC_LIB_EXTS ".a", ".lib"

struct find_library_result
find_library(struct workspace *wk, obj compiler, const char *libname, obj extra_dirs, enum find_library_flag flags);
void
find_library_result_to_dependency(struct workspace *wk, struct find_library_result find_result, obj compiler, obj d);

enum compiler_check_mode {
	compiler_check_mode_preprocess,
	compiler_check_mode_compile,
	compiler_check_mode_link,
	compiler_check_mode_run,
};

struct compiler_check_opts {
	struct run_cmd_ctx cmd_ctx;
	enum compiler_check_mode mode;
	obj comp_id;
	struct args_kw *deps, *inc, *required, *werror;
	obj args;
	bool skip_run_check;
	bool src_is_path;
	bool keep_cmd_ctx;
	bool output_is_stdout;
	const char *output_path;

	bool from_cache;
	obj cache_key, cache_val;
};

bool
compiler_check(struct workspace *wk, struct compiler_check_opts *opts, const char *src, uint32_t err_node, bool *res);

FUNC_REGISTER(compiler);
#endif
