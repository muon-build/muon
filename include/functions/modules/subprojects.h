/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_MODULES_SUBPROJECTS_H
#define MUON_FUNCTIONS_MODULES_SUBPROJECTS_H

#include "lang/func_lookup.h"
#include "platform/timer.h"

struct subprojects_common_ctx {
	struct arr handlers;
	struct timer duration;
	uint32_t failed;
	bool force, print;
	obj *res;
};

typedef enum iteration_result (
	*subprojects_foreach_cb)(struct workspace *wk, struct subprojects_common_ctx *ctx, const char *name);

bool subprojects_foreach(struct workspace *wk, obj list, struct subprojects_common_ctx *usr_ctx, subprojects_foreach_cb cb);

extern const struct func_impl impl_tbl_module_subprojects[];
#endif
