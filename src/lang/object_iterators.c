#include "compat.h"

#include "lang/object_iterators.h"
#include "lang/workspace.h"

obj
obj_array_flat_iter_next(struct workspace *wk, obj arr, struct obj_array_flat_iter_ctx *ctx)
{
	obj v = 0;

	if (!ctx->init) {
		ctx->a = get_obj_array(wk, arr);
		ctx->pushed = 0;
		ctx->init = true;
	}

	while (ctx->a && !v) {
		v = ctx->a->val;

		while (get_obj_type(wk, v) == obj_array) {
			stack_push(&wk->stack, ctx->a, get_obj_array(wk, v));
			++ctx->pushed;
			v = ctx->a->val;
		}

		while (!ctx->a->have_next && ctx->pushed) {
			stack_pop(&wk->stack, ctx->a);
			--ctx->pushed;
		}

		if (ctx->a->have_next) {
			ctx->a = get_obj_array(wk, ctx->a->next);
		} else {
			ctx->a = 0;
		}
	}

	return v;
}

void
obj_array_flat_iter_end(struct workspace *wk, struct obj_array_flat_iter_ctx *ctx)
{
	while (ctx->pushed) {
		stack_pop(&wk->stack, ctx->a);
		--ctx->pushed;
	}
}
