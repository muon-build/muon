/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "lang/func_lookup.h"
#include "functions/disabler.h"
#include "lang/typecheck.h"

static bool
func_disabler_found(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, false);
	return true;
}

const struct func_impl impl_tbl_disabler[] = {
	{ "found", func_disabler_found, tc_bool },
	{ NULL, NULL },
};
