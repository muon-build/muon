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

static bool
func_both_libs_get_shared_lib(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_both_libs(wk, self)->dynamic_lib;
	return true;
}

static bool
func_both_libs_get_static_lib(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_both_libs(wk, self)->static_lib;
	return true;
}

static obj
both_libs_self_transform(struct workspace *wk, obj self)
{
	return decay_both_libs(wk, self);
}

void
both_libs_build_impl_tbl(void)
{
	uint32_t i;
	for (i = 0; impl_tbl_build_target[i].name; ++i) {
		struct func_impl tmp = impl_tbl_build_target[i];
		tmp.self_transform = both_libs_self_transform;
		impl_tbl_both_libs[i] = tmp;
	}
}

struct func_impl impl_tbl_both_libs[] = {
	[ARRAY_LEN(impl_tbl_build_target) - 1] = { "get_shared_lib", func_both_libs_get_shared_lib, tc_build_target },
	{ "get_static_lib", func_both_libs_get_static_lib, tc_build_target },
	{ NULL, NULL },
};
