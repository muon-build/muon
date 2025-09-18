/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/include_directory.h"
#include "lang/typecheck.h"

FUNC_IMPL(include_directory, full_path, tc_string)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	*res = get_obj_include_directory(wk, self)->path;
	return true;
}

FUNC_REGISTER(include_directory)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(include_directory, full_path);
	}
}
