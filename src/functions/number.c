/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>

#include "functions/number.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"

FUNC_IMPL(number, is_odd, tc_bool)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, (get_obj_number(wk, self) & 1) != 0);
	return true;
}

FUNC_IMPL(number, is_even, tc_bool)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, (get_obj_number(wk, self) & 1) == 0);
	return true;
}

FUNC_IMPL(number, to_string, tc_string)
{
	enum kwargs { kw_fill };
	struct args_kw akw[] = { [kw_fill] = { "fill", tc_number }, 0 };
	if (!pop_args(wk, NULL, akw)) {
		return false;
	}

	char fmt[32];
	int64_t fill = akw[kw_fill].set ? get_obj_number(wk, akw[kw_fill].val) : 0;
	if (fill > 0) {
		snprintf(fmt, sizeof(fmt), "%%0%" PRId64 PRId64, get_obj_number(wk, akw[kw_fill].val));
	} else {
		snprintf(fmt, sizeof(fmt), "%%" PRId64);
	}

	*res = make_strf(wk, fmt, get_obj_number(wk, self));
	return true;
}

FUNC_REGISTER(number)
{
	FUNC_IMPL_REGISTER(number, to_string);
	FUNC_IMPL_REGISTER(number, is_even);
	FUNC_IMPL_REGISTER(number, is_odd);
}
