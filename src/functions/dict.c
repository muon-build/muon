/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/dict.h"
#include "lang/typecheck.h"

static enum iteration_result
dict_keys_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	obj *arr = _ctx;

	obj_array_push(wk, *arr, k);

	return ir_cont;
}

FUNC_IMPL(dict, keys, tc_array)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj(wk, obj_array);
	obj_dict_foreach(wk, self, res, dict_keys_iter);

	return true;
}

FUNC_IMPL(dict, has_key, tc_bool)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, obj_dict_in(wk, self, an[0].val));
	return true;
}

FUNC_IMPL(dict, get, tc_any)
{
	struct args_norm an[] = { { obj_string }, { tc_any, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	if (!obj_dict_index(wk, self, an[0].val, res)) {
		if (an[1].set) {
			*res = an[1].val;
		} else {
			vm_error_at(wk, an[0].node, "key not in dictionary: '%s'", get_cstr(wk, an[0].val));
			return false;
		}
	}
	return true;
}

FUNC_IMPL(dict, delete, 0, func_impl_flag_impure, .desc = "Delete a key from a dictionary.  Note that this mutates the underlying dictionary and also may reorder its keys.")
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	if (!obj_dict_del(wk, self, an[0].val)) {
		vm_error_at(wk, an[0].node, "key not in dictionary: '%s'", get_cstr(wk, an[0].val));
		return false;
	}
	return true;
}

FUNC_IMPL(dict, set, 0, func_impl_flag_impure)
{
	struct args_norm an[] = { { tc_string }, { tc_any }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj_dict_set(wk, self, an[0].val, an[1].val);
	return true;
}

FUNC_REGISTER(dict)
{
	FUNC_IMPL_REGISTER(dict, keys);
	FUNC_IMPL_REGISTER(dict, has_key);
	FUNC_IMPL_REGISTER(dict, get);

	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(dict, delete);
		FUNC_IMPL_REGISTER(dict, set);
	}
}
