/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_KERNEL_SUBPROJECT_H
#define MUON_FUNCTIONS_KERNEL_SUBPROJECT_H

#include "lang/func_lookup.h"

bool subproject(struct workspace *wk,
	obj name,
	enum requirement_type req,
	struct args_kw *default_options,
	struct args_kw *versions,
	obj *res);

FUNC_REGISTER(kernel_subproject);
#endif
