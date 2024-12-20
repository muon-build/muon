/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_COMPILER_H
#define MUON_FUNCTIONS_COMPILER_H
#include "lang/func_lookup.h"


enum find_library_flag {
	find_library_flag_only_static = 1 << 0,
	find_library_flag_prefer_static = 1 << 0,
};

#define COMPILER_DYNAMIC_LIB_EXTS ".so", ".dylib", ".dll"
#define COMPILER_STATIC_LIB_EXTS ".a", ".lib"

obj find_library(struct workspace *wk, const char *name, obj libdirs, enum find_library_flag flags);

extern const struct func_impl impl_tbl_compiler[];
extern const struct func_impl impl_tbl_compiler_internal[];
#endif
