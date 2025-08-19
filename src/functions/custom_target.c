/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "lang/func_lookup.h"
#include "functions/custom_target.h"
#include "functions/file.h"
#include "lang/typecheck.h"

bool
custom_target_is_linkable(struct workspace *wk, obj ct)
{
	struct obj_custom_target *tgt = get_obj_custom_target(wk, ct);

	if (get_obj_array(wk, tgt->output)->len == 1) {
		obj out = obj_array_index(wk, tgt->output, 0);

		return file_is_linkable(wk, out);
	}

	return false;
}

FUNC_IMPL(custom_target, to_list, tc_array, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_custom_target(wk, self)->output;
	return true;
}

FUNC_IMPL(custom_target, full_path, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	obj elem;
	if (!obj_array_flatten_one(wk, get_obj_custom_target(wk, self)->output, &elem)) {
		vm_error(wk, "this custom_target has multiple outputs");
		return false;
	}

	*res = *get_obj_file(wk, elem);
	return true;
}

FUNC_REGISTER(custom_target)
{
	FUNC_IMPL_REGISTER(custom_target, full_path);
	FUNC_IMPL_REGISTER(custom_target, to_list);
}
