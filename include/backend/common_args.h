/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_COMMON_ARGS_H
#define MUON_BACKEND_COMMON_ARGS_H

#include "lang/workspace.h"

void ca_get_std_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args);
void ca_get_option_compile_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args);
void ca_get_option_link_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args);

obj ca_build_target_joined_args(struct workspace *wk, obj processed_args);

bool
ca_prepare_target_linker_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	struct obj_build_target *tgt,
	bool relativize);

bool ca_prepare_all_targets(struct workspace *wk);

void ca_setup_compiler_args_includes(struct workspace *wk, obj compiler, obj include_dirs, obj args, bool relativize);

void ca_relativize_paths(struct workspace *wk, obj arr, bool relativize_strings, obj *res);
void ca_relativize_path(struct workspace *wk, obj path, bool relativize_strings, obj *res);
void ca_relativize_path_push(struct workspace *wk, obj path, obj arr);

obj ca_regenerate_build_command(struct workspace *wk, bool opts_only);

obj ca_backend_tgt_name(struct workspace *wk, obj tgt_id);
#endif
