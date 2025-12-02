/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_KERNEL_H
#define MUON_FUNCTIONS_KERNEL_H
#include "lang/func_lookup.h"

struct find_program_ctx {
	struct args_kw *default_options;
	obj *res;
	uint32_t node;
	obj version, version_argument;
	obj dirs;
	enum requirement_type requirement;
	enum machine_kind machine;
	bool found;
};

bool find_program(struct workspace *wk, struct find_program_ctx *ctx, obj prog);
bool find_program_check_override(struct workspace *wk, struct find_program_ctx *ctx, obj prog);

FUNC_REGISTER(kernel);
#endif
