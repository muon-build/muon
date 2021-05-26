#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "interpreter.h"
#include "log.h"
#include "object.h"
#include "parser.h"

const char *
obj_type_to_s(enum obj_type t)
{
	switch (t) {
	case obj_any: return "any";
	case obj_default: return "default";
	case obj_null: return "null";
	case obj_compiler: return "compiler";
	case obj_dependency: return "dependency";
	case obj_meson: return "meson";
	case obj_string: return "string";
	case obj_number: return "number";
	case obj_array: return "array";
	case obj_bool: return "bool";
	case obj_file: return "file";
	case obj_build_target: return "build_target";
	case obj_subproject: return "subproject";
	case obj_type_count: assert(false); return "uh oh";
	case obj_function: return "function";
	}

	assert(false && "unreachable");
	return NULL;
}

bool
obj_equal(struct workspace *wk, uint32_t l_id, uint32_t r_id)
{
	if (l_id == r_id) {
		return true;
	}

	struct obj *l = get_obj(wk, l_id),
		   *r = get_obj(wk, r_id);

	if (l->type != r->type) {
		return false;
	}

	switch (l->type) {
	case obj_string:
		return strcmp(wk_str(wk, l->dat.str), wk_str(wk, r->dat.str)) == 0;
	case obj_number:
		return l->dat.num == r->dat.num;
	case obj_bool:
		return l->dat.boolean == r->dat.boolean;
	case obj_array:
	case obj_file:
		L(log_interp, "TODO: compare %s", obj_type_to_s(l->type));
		return false;
	default:
		return false;
	}
}

bool
obj_array_foreach(struct workspace *wk, uint32_t arr_id, void *ctx, obj_array_iterator cb)
{
	struct obj *arr_elem = get_obj(wk, arr_id);
	assert(arr_elem->type == obj_array);

	if (!arr_elem->dat.arr.len) {
		return true;
	}

	while (true) {
		switch (cb(wk, ctx, arr_elem->dat.arr.l)) {
		case ir_cont:
			break;
		case ir_done:
			return true;
		case ir_err:
			return false;
		}

		if (!arr_elem->dat.arr.have_r) {
			break;
		}
		arr_elem = get_obj(wk, arr_elem->dat.arr.r);
	}

	return true;
}

void
obj_array_push(struct workspace *wk, uint32_t arr_id, uint32_t child_id)
{
	uint32_t child_arr_id;
	struct obj *arr, *tail, *child_arr;

	if (!(arr = get_obj(wk, arr_id))->dat.arr.len) {
		arr->dat.arr.tail = arr_id;
		arr->dat.arr.len = 1;
		arr->dat.arr.l = child_id;
		return;
	}

	child_arr = make_obj(wk, &child_arr_id, obj_array);
	child_arr->dat.arr.l = child_id;

	arr = get_obj(wk, arr_id);
	assert(arr->type == obj_array);

	tail = get_obj(wk, arr->dat.arr.tail);
	assert(tail->type == obj_array);
	assert(!tail->dat.arr.have_r);
	tail->dat.arr.have_r = true;
	tail->dat.arr.r = child_arr_id;

	arr->dat.arr.tail = child_arr_id;
	++arr->dat.arr.len;
}

struct obj_array_in_iter_ctx {
	uint32_t l_id;
	bool res;
};

static enum iteration_result
obj_array_in_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct obj_array_in_iter_ctx *ctx = _ctx;

	if (obj_equal(wk, ctx->l_id, v_id)) {
		ctx->res = true;
		return ir_done;
	}

	return ir_cont;
}

bool
obj_array_in(struct workspace *wk, uint32_t l_id, uint32_t r_id, bool *res)
{
	struct obj_array_in_iter_ctx ctx = { .l_id = l_id };
	if (!obj_array_foreach(wk, r_id, &ctx, obj_array_in_iter)) {
		return false;
	}

	*res = ctx.res;
	return true;
}

struct obj_array_index_iter_ctx { uint32_t res, i, tgt; };

static enum iteration_result
obj_array_index_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct obj_array_index_iter_ctx *ctx = _ctx;

	if (ctx->i == ctx->tgt) {
		ctx->res = v_id;
		return ir_done;
	}

	++ctx->i;
	return ir_cont;
}

bool
obj_array_index(struct workspace *wk, uint32_t arr_id, int64_t i, uint32_t *res)
{
	struct obj_array_index_iter_ctx ctx = { .tgt = i };

	if (!obj_array_foreach(wk, arr_id, &ctx, obj_array_index_iter)) {
		return false;
	}

	*res = ctx.res;
	return true;
}

struct obj_array_dup_ctx { uint32_t *arr; };

static enum iteration_result
obj_array_dup_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct obj_array_dup_ctx *ctx = _ctx;

	obj_array_push(wk, *ctx->arr, v_id);

	return ir_cont;
}

bool
obj_array_dup(struct workspace *wk, uint32_t arr_id, uint32_t *res)
{
	struct obj_array_dup_ctx ctx = { .arr = res };

	make_obj(wk, res, obj_array);

	if (!obj_array_foreach(wk, arr_id, &ctx, obj_array_dup_iter)) {
		return false;
	}

	return true;
}

void
obj_array_extend(struct workspace *wk, uint32_t a_id, uint32_t b_id)
{
	struct obj *a, *b, *tail;

	assert(get_obj(wk, a_id)->type == obj_array && get_obj(wk, b_id)->type == obj_array);

	if (!(b = get_obj(wk, b_id))->dat.arr.len) {
		return;
	}

	if (!(a = get_obj(wk, a_id))->dat.arr.len) {
		*a = *b;
		return;
	}

	tail = get_obj(wk, a->dat.arr.tail);
	assert(tail->type == obj_array);
	assert(!tail->dat.arr.have_r);
	tail->dat.arr.have_r = true;
	tail->dat.arr.r = b_id;

	a->dat.arr.tail = b_id;
	a->dat.arr.len += b->dat.arr.len;
}
