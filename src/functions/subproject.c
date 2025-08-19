/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/subproject.h"
#include "lang/analyze.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"

bool
subproject_get_variable(struct workspace *wk, uint32_t node, obj name_id, obj fallback, obj subproj, obj *res)
{
	const char *name = get_cstr(wk, name_id);
	struct obj_subproject *sub = get_obj_subproject(wk, subproj);

	if (!sub->found) {
		if (wk->vm.in_analyzer) {
			*res = make_typeinfo(wk, tc_any);
			return true;
		} else {
			vm_error_at(wk, node, "subproject was not found");
		}
		return false;
	}

	bool ok = true;
	stack_push(&wk->stack, wk->vm.scope_stack, ((struct project *)arr_get(&wk->projects, sub->id))->scope_stack);
	if (!wk->vm.behavior.get_variable(wk, name, res)) {
		if (!fallback) {
			ok = false;
		} else {
			*res = fallback;
		}
	}
	stack_pop(&wk->stack, wk->vm.scope_stack);

	return ok;
}

FUNC_IMPL(subproject, get_variable, tc_any)
{
	struct args_norm an[] = { { obj_string }, { tc_any, .optional = true }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	if (!subproject_get_variable(wk, an[0].node, an[0].val, an[1].val, self, res)) {
		vm_error_at(wk, an[0].node, "subproject does not define '%s'", get_cstr(wk, an[0].val));
		return false;
	}
	return true;
}

FUNC_IMPL(subproject, found, tc_bool, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, get_obj_subproject(wk, self)->found);
	return true;
}

FUNC_REGISTER(subproject)
{
	FUNC_IMPL_REGISTER(subproject, found);
	FUNC_IMPL_REGISTER(subproject, get_variable);
}
