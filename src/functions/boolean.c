/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "functions/boolean.h"
#include "lang/typecheck.h"

FUNC_IMPL(boolean, to_string, tc_string, func_impl_flag_pure)
{
	struct args_norm an[] = { { obj_string, .optional = true }, { obj_string, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	if (get_obj_bool(wk, self)) {
		*res = an[0].set ? an[0].val : make_str(wk, "true");
	} else {
		*res = an[1].set ? an[1].val : make_str(wk, "false");
	}

	return true;
}

FUNC_IMPL(boolean, to_int, tc_number, func_impl_flag_pure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	int32_t val = get_obj_bool(wk, self) ? 1 : 0;
	*res = make_obj(wk, obj_number);
	set_obj_number(wk, *res, val);
	return true;
}

FUNC_REGISTER(boolean)
{
	FUNC_IMPL_REGISTER(boolean, to_int);
	FUNC_IMPL_REGISTER(boolean, to_string);
}
