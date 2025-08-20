/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_KERNEL_CUSTOM_TARGET_H
#define MUON_FUNCTIONS_KERNEL_CUSTOM_TARGET_H
#include "lang/func_lookup.h"

struct make_custom_target_opts {
	obj name;
	uint32_t input_node;
	uint32_t output_node;
	uint32_t command_node;
	obj input_orig;
	obj output_orig;
	const char *output_dir, *build_dir;
	obj command_orig;
	obj depfile_orig;
	obj extra_args;
	bool capture;
	bool feed;
	bool extra_args_valid, extra_args_used;
};

bool make_custom_target(struct workspace *wk, struct make_custom_target_opts *opts, obj *res);

bool
install_custom_target(struct workspace *wk,
	struct obj_custom_target *tgt,
	const struct args_kw *kw_install,
	const struct args_kw *kw_build_by_default,
	obj install_dir,
	obj install_mode);

struct process_custom_target_commandline_opts {
	uint32_t err_node;
	bool relativize;
	obj name;
	obj input;
	obj output;
	obj depfile;
	obj depends;
	obj extra_args;
	const char *build_dir;
	bool extra_args_valid, extra_args_used;
};

bool process_custom_target_commandline(struct workspace *wk,
	struct process_custom_target_commandline_opts *opts,
	obj arr,
	obj *res);

FUNC_REGISTER(kernel_custom_target);
#endif
