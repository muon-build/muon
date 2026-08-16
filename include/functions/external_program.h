/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_EXTERNAL_PROGRAM_H
#define MUON_FUNCTIONS_EXTERNAL_PROGRAM_H
#include "lang/func_lookup.h"

enum obj_external_program_cmd_array_flag {
	obj_external_program_cmd_array_flag_prefer_argv0 = 1 << 0,
};

obj obj_external_program_cmd_array(struct workspace *wk,
	struct obj_external_program *ep,
	enum obj_external_program_cmd_array_flag flags);

void find_program_guess_version(struct workspace *wk, struct obj_external_program *ep, obj version_argument);

FUNC_REGISTER(external_program);
#endif
