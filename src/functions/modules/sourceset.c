/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/modules/sourceset.h"
#include "lang/typecheck.h"

FUNC_IMPL(module_source_set, source_set, tc_source_set, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj(wk, obj_source_set);
	struct obj_source_set *ss = get_obj_source_set(wk, *res);
	ss->rules = make_obj(wk, obj_array);
	return true;
}

FUNC_REGISTER(module_source_set)
{
	FUNC_IMPL_REGISTER(module_source_set, source_set);
}
