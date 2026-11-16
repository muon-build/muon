/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_KERNEL_OPTIONS_H
#define MUON_FUNCTIONS_KERNEL_OPTIONS_H

#include "lang/func_lookup.h"

FUNC_REGISTER(kernel_options);

struct func_option_common_result {
	obj option;
	enum create_option_flag flags;
};

bool func_option_common(struct workspace *wk, struct args_norm *an, struct func_option_common_result *res);
bool func_option_bind(struct workspace *wk, uint32_t node, obj name, obj option, enum create_option_flag flags);
#endif
