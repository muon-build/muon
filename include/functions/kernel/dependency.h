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
#endif
