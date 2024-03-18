/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/common.h"
#include "functions/subproject.h"
#include "lang/typecheck.h"
#include "log.h"

bool
subproject_get_variable(struct workspace *wk, uint32_t node, obj name_id,
	obj fallback, obj subproj, obj *res)
{
	const char *name = get_cstr(wk, name_id);
	struct obj_subproject *sub = get_obj_subproject(wk, subproj);

	if (!sub->found) {
		vm_error_at(wk, node, "subproject was not found");
		return false;
	}

	if (!wk->vm.behavior.get_variable(wk, name, res)) {
		if (!fallback) {
			return false;
		} else {
			*res = fallback;
		}
	}

	return true;
}

static bool
func_subproject_get_variable(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { tc_any }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, ao, NULL)) {
		return false;
	}

	if (!subproject_get_variable(wk, an[0].node, an[0].val, ao[0].val, rcvr, res)) {
		vm_error_at(wk, an[0].node, "subproject does not define '%s'", get_cstr(wk, an[0].val));
		return false;
	}
	return true;
}

static bool
func_subproject_found(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, get_obj_subproject(wk, rcvr)->found);
	return true;
}

const struct func_impl impl_tbl_subproject[] = {
	{ "found", func_subproject_found, tc_bool },
	{ "get_variable", func_subproject_get_variable, tc_any },
	{ NULL, NULL },
};
