/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/array.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "util.h"

FUNC_IMPL(array, length, tc_number)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj(wk, obj_number);
	set_obj_number(wk, *res, get_obj_array(wk, self)->len);
	return true;
}

FUNC_IMPL(array, get, tc_any)
{
	struct args_norm an[] = { { obj_number }, { tc_any, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	int64_t i = get_obj_number(wk, an[0].val);

	if (!bounds_adjust(get_obj_array(wk, self)->len, &i)) {
		if (an[1].set) {
			*res = an[1].val;
		} else {
			vm_error_at(wk, an[0].node, "index out of bounds");
			return false;
		}
	} else {
		*res = obj_array_index(wk, self, i);
	}

	return true;
}

struct array_contains_ctx {
	obj item;
	bool found;
};

static enum iteration_result
array_contains_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct array_contains_ctx *ctx = _ctx;

	if (get_obj_type(wk, val) == obj_array) {
		obj_array_foreach(wk, val, ctx, array_contains_iter);
		if (ctx->found) {
			return ir_done;
		}
	}

	if (obj_equal(wk, val, ctx->item)) {
		ctx->found = true;
		return ir_done;
	}

	return ir_cont;
}

FUNC_IMPL(array, contains, tc_bool)
{
	struct args_norm an[] = { { tc_any }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	struct array_contains_ctx ctx = { .item = an[0].val };
	obj_array_foreach(wk, self, &ctx, array_contains_iter);

	*res = make_obj_bool(wk, ctx.found);
	return true;
}

FUNC_IMPL(array, delete, 0, func_impl_flag_impure)
{
	struct args_norm an[] = { { tc_number }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	int64_t idx = get_obj_number(wk, an[0].val);
	if (!boundscheck(wk, an[0].node, get_obj_array(wk, self)->len, &idx)) {
		return false;
	}

	obj_array_del(wk, self, idx);
	return true;
}

FUNC_IMPL(array, slice, tc_array)
{
	struct args_norm an[] = { { obj_number, .optional = true }, { obj_number, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const struct obj_array *a = get_obj_array(wk, self);
	int64_t start = 0, end = a->len;

	if (an[0].set) {
		start = get_obj_number(wk, an[0].val);
	}

	if (an[1].set) {
		end = get_obj_number(wk, an[1].val);
	}

	bounds_adjust(a->len, &start);
	bounds_adjust(a->len, &end);

	end = MIN(end, a->len);

	*res = obj_array_slice(wk, self, start, end);
	return true;
}

FUNC_IMPL(array, clear, 0, func_impl_flag_impure)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	obj_array_clear(wk, self);
	return true;
}

FUNC_IMPL(array, dedup, tc_array)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	obj_array_dedup(wk, self, res);
	return true;
}

FUNC_IMPL(array, flatten, tc_array)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	*res = make_obj(wk, obj_array);
	obj v;
	obj_array_flat_for(wk, self, v) {
		obj_array_push(wk, *res, v);
	}
	return true;
}

FUNC_REGISTER(array)
{
	FUNC_IMPL_REGISTER(array, length);
	FUNC_IMPL_REGISTER(array, get);
	FUNC_IMPL_REGISTER(array, contains);

	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(array, delete);
		FUNC_IMPL_REGISTER(array, slice);
		FUNC_IMPL_REGISTER(array, clear);
		FUNC_IMPL_REGISTER(array, dedup);
		FUNC_IMPL_REGISTER(array, flatten);
	}
}
