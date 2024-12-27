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

#define COMPILER_DYNAMIC_LIB_EXTS ".so", ".dylib", ".dll.a", ".dll"
#define COMPILER_STATIC_LIB_EXTS ".a", ".lib"

obj find_library(struct workspace *wk, const char *name, obj libdirs, enum find_library_flag flags);
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
	const char *output_path;

	bool from_cache;
	obj cache_key, cache_val;
};

bool
compiler_check(struct workspace *wk, struct compiler_check_opts *opts, const char *src, uint32_t err_node, bool *res);

extern const struct func_impl impl_tbl_compiler[];
extern const struct func_impl impl_tbl_compiler_internal[];
#endif
