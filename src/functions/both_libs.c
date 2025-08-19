/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "buf_size.h"
#include "error.h"
#include "functions/both_libs.h"
#include "functions/build_target.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"
#include "options.h"
#include "platform/assert.h"

obj
decay_both_libs(struct workspace *wk, obj both_libs)
{
	struct obj_both_libs *b = get_obj_both_libs(wk, both_libs);

	enum default_both_libraries def_both_libs = b->default_both_libraries;

	if (def_both_libs == default_both_libraries_auto) {
		 def_both_libs = get_option_default_both_libraries(wk, 0, 0);
	}

	switch(def_both_libs) {
	case default_both_libraries_auto:
		return b->dynamic_lib;
	case default_both_libraries_static:
		return b->static_lib;
	case default_both_libraries_shared:
		return b->dynamic_lib;
	}

	UNREACHABLE_RETURN;
}

FUNC_IMPL(both_libs, get_shared_lib, tc_build_target, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_both_libs(wk, self)->dynamic_lib;
	return true;
}

FUNC_IMPL(both_libs, get_static_lib, tc_build_target, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_both_libs(wk, self)->static_lib;
	return true;
}

FUNC_REGISTER(both_libs)
{
	FUNC_REGISTER_INHERIT(build_target, decay_both_libs);

	FUNC_IMPL_REGISTER(both_libs, get_shared_lib);
	FUNC_IMPL_REGISTER(both_libs, get_static_lib);
}
