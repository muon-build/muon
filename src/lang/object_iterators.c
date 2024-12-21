/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "lang/object_iterators.h"
#include "lang/workspace.h"

obj
obj_array_flat_iter_next(struct workspace *wk, obj arr, struct obj_array_flat_iter_ctx *ctx)
{
	obj v = 0;

	if (!ctx->init) {
		struct obj_array *a = get_obj_array(wk, arr);
		ctx->e = a->len ? bucket_arr_get(&wk->vm.objects.array_elems, a->head) : 0;
		ctx->pushed = 0;
		ctx->init = true;
	}

	while (ctx->e && !v) {
		v = ctx->e->val;

		while (get_obj_type(wk, v) == obj_array) {
			struct obj_array *a = get_obj_array(wk, v);
			struct obj_array_elem *e = 0;
			if (!a->len) {
				v = 0;
				goto skip;
			}

			e = bucket_arr_get(&wk->vm.objects.array_elems, a->head);
			v = e->val;
			stack_push(&wk->stack, ctx->e, e);
			++ctx->pushed;
		}

#if 0
	Note: Below was taken from the original implementation of
		  obj_array_foreach_flat.  May want to integrate the typeinfo
		  handling at some point.

	if (get_obj_type(wk, val) == obj_array) {
		if (!obj_array_foreach(wk, val, ctx, obj_array_foreach_flat_iter)) {
			return ir_err;
		} else {
			return ir_cont;
		}
	} else if (get_obj_type(wk, val) == obj_typeinfo && get_obj_typeinfo(wk, val)->type == tc_array) {
		// skip typeinfo arrays as they wouldn't be yielded if they
		// were real arrays
		return ir_cont;
	} else {
		return ctx->cb(wk, ctx->usr_ctx, val);
	}
#endif

skip:

		while (!ctx->e->next && ctx->pushed) {
			stack_pop(&wk->stack, ctx->e);
			--ctx->pushed;
		}

		if (ctx->e->next) {
			ctx->e = bucket_arr_get(&wk->vm.objects.array_elems, ctx->e->next);
		} else {
			ctx->e = 0;
		}
	}

	return v;
}

void
obj_array_flat_iter_end(struct workspace *wk, struct obj_array_flat_iter_ctx *ctx)
{
	while (ctx->pushed) {
		stack_pop(&wk->stack, ctx->e);
		--ctx->pushed;
	}
}
