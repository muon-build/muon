/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>

#include "functions/number.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"
#include "log.h"

static bool
func_number_is_odd(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, (get_obj_number(wk, self) & 1) != 0);
	return true;
}

static bool
func_number_is_even(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, (get_obj_number(wk, self) & 1) == 0);
	return true;
}

static bool
func_number_to_string(struct workspace *wk, obj self, obj *res)
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

const struct func_impl impl_tbl_number[] = {
	{ "to_string", func_number_to_string, tc_string },
	{ "is_even", func_number_is_even, tc_bool },
	{ "is_odd", func_number_is_odd, tc_bool },
	{ NULL, NULL },
};
