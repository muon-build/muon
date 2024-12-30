/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_KERNEL_DEPENDENCY_H
#define MUON_FUNCTIONS_KERNEL_DEPENDENCY_H
#include "lang/workspace.h"


enum build_dep_merge_flag {
	build_dep_merge_flag_merge_all = 1 << 0,
};

void
build_dep_merge(struct workspace *wk, struct build_dep *dest, const struct build_dep *src, enum build_dep_merge_flag flags);
void dep_process_deps(struct workspace *wk, obj deps, struct build_dep *dest);
bool dep_process_link_with(struct workspace *wk, uint32_t err_node, obj arr, struct build_dep *dest);
bool dep_process_link_whole(struct workspace *wk, uint32_t err_node, obj arr, struct build_dep *dest);
void dep_process_includes(struct workspace *wk, obj arr, enum include_type include_type, obj dest);

void build_dep_init(struct workspace *wk, struct build_dep *dep);

bool func_dependency(struct workspace *wk, obj self, obj *res);
bool func_declare_dependency(struct workspace *wk, obj _, obj *res);

enum dependency_lookup_method {
	// Auto means to use whatever dependency checking mechanisms in whatever order meson thinks is best.
	dependency_lookup_method_auto,
	dependency_lookup_method_pkgconfig,
	// The dependency is provided by the standard library and does not need to be linked
	dependency_lookup_method_builtin,
	// Just specify the standard link arguments, assuming the operating system provides the library.
	dependency_lookup_method_system,
	// This is only supported on OSX - search the frameworks directory by name.
	dependency_lookup_method_extraframework,
	// Detect using the sysconfig module.
	dependency_lookup_method_sysconfig,
	// Specify using a "program"-config style tool
	dependency_lookup_method_config_tool,
	// Misc
	dependency_lookup_method_dub,
	dependency_lookup_method_cmake,
};
bool dependency_lookup_method_from_s(const struct str *s, enum dependency_lookup_method *lookup_method);
const char *dependency_lookup_method_to_s(enum dependency_lookup_method method);
#endif
