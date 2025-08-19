/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/disabler.h"
#include "lang/typecheck.h"

FUNC_IMPL(disabler, found, tc_bool)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = obj_bool_false;
	return true;
}

FUNC_REGISTER(disabler)
{
	FUNC_IMPL_REGISTER(disabler, found);
}
