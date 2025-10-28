/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>

#include "coerce.h"
#include "functions/modules/util.h"
#include "lang/serial.h"
#include "lang/typecheck.h"
#include "platform/filesystem.h"

FUNC_IMPL(module_util, repr, tc_string, .desc = "return a string representing the passed object")
{
	struct args_norm an[] = { { tc_any | TYPE_TAG_ALLOW_NULL }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, 0)) {
		return false;
	}

	TSTR(buf);
	obj_to_s(wk, an[0].val, &buf);

	*res = tstr_into_str(wk, &buf);
	return true;
}

FUNC_IMPL(module_util, serial_load, tc_any)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj str;
	coerce_string(wk, an[0].node, an[0].val, &str);

	FILE *f;
	if (str_eql(get_str(wk, str), &STR("-"))) {
		f = stdin;
	} else if (!(f = fs_fopen(get_cstr(wk, str), "rb"))) {
		return false;
	}

	bool ret = false;
	if (!serial_load(wk, res, f)) {
		goto ret;
	}

	if (!fs_fclose(f)) {
		goto ret;
	}

	ret = true;
ret:
	return ret;
}

FUNC_IMPL(module_util, serial_dump, .flags = func_impl_flag_sandbox_disable)
{
	struct args_norm an[] = { { tc_string | tc_file }, { tc_any }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj str;
	coerce_string(wk, an[0].node, an[0].val, &str);

	FILE *f;
	if (!(f = fs_fopen(get_cstr(wk, str), "wb"))) {
		return false;
	}

	bool ret = false;
	if (!serial_dump(wk, an[1].val, f)) {
		goto ret;
	}

	if (!fs_fclose(f)) {
		goto ret;
	}

	ret = true;
ret:
	return ret;
}

FUNC_IMPL(module_util, exit, 0)
{
	struct args_norm an[] = { { tc_number }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	exit(get_obj_number(wk, an[0].val));

	return true;
}

FUNC_REGISTER(module_util)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_util, repr);
		FUNC_IMPL_REGISTER(module_util, serial_dump);
		FUNC_IMPL_REGISTER(module_util, serial_load);
		FUNC_IMPL_REGISTER(module_util, exit);
	}
}
