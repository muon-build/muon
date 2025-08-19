/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "lang/func_lookup.h"
#include "functions/source_configuration.h"
#include "lang/typecheck.h"

FUNC_IMPL(source_configuration, sources, tc_array)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_source_configuration(wk, self)->sources;
	return true;
}

FUNC_IMPL(source_configuration, dependencies, tc_array)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_source_configuration(wk, self)->dependencies;
	return true;
}

FUNC_REGISTER(source_configuration)
{
	FUNC_IMPL_REGISTER(source_configuration, sources);
	FUNC_IMPL_REGISTER(source_configuration, dependencies);
}
