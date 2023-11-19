/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_SUBPROJECT_H
#define MUON_FUNCTIONS_SUBPROJECT_H
#include "functions/common.h"

bool subproject_get_variable(struct workspace *wk, uint32_t node, obj name_id,
	obj fallback, obj subproj, obj *res);

extern const struct func_impl impl_tbl_subproject[];
#endif
