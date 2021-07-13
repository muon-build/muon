#include "posix.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "functions/common.h"
#include "hash.h"
#include "interpreter.h"
#include "log.h"
#include "parser.h"
#include "path.h"
#include "workspace.h"

__attribute__ ((format(printf, 3, 4)))
void
interp_error(struct workspace *wk, uint32_t n_id, const char *fmt, ...)
{
	struct node *n = get_node(wk->ast, n_id);

	va_list args;
	va_start(args, fmt);
	error_message(wk->src, n->line, n->col, fmt, args);
	va_end(args);
}

bool
boundscheck(struct workspace *wk, uint32_t n_id, uint32_t obj_id, int64_t *i)
{
	struct obj *arr = get_obj(wk, obj_id);

	assert(arr->type == obj_array);

	if (labs(*i) >= arr->dat.arr.len) {
		interp_error(wk, n_id, "index %ld out of bounds", *i);
		return false;
	}

	if (*i < 0) {
		*i += arr->dat.n;
	}

	return true;
}

bool
typecheck(struct workspace *wk, uint32_t n_id, uint32_t obj_id, enum obj_type type)
{
	struct obj *obj = get_obj(wk, obj_id);

	if (type != obj_any && obj->type != type) {
		interp_error(wk, n_id, "expected type %s, got %s", obj_type_to_s(type), obj_type_to_s(obj->type));
		return false;
	}

	return true;
}

static bool interp_chained(struct workspace *wk, uint32_t node_id, uint32_t l_id, uint32_t *obj);

static bool
interp_method(struct workspace *wk, uint32_t node_id, uint32_t l_id, uint32_t *obj)
{
	uint32_t result = 0;

	struct node *n = get_node(wk->ast, node_id);

	if (!builtin_run(wk, true, l_id, node_id, &result)) {
		return false;
	}

	if (n->chflg & node_child_d) {
		return interp_chained(wk, n->d, result, obj);
	} else {
		*obj = result;
		return true;
	}
}

static bool
interp_index(struct workspace *wk, struct node *n, uint32_t l_id, uint32_t *obj)
{
	uint32_t r_id;

	if (!interp_node(wk, n->r, &r_id)) {
		return false;
	}

	struct obj *l = get_obj(wk, l_id), *r = get_obj(wk, r_id);
	switch (l->type) {
	case obj_array: {
		if (!typecheck(wk, n->r, r_id, obj_number)) {
			return false;
		}

		int64_t i = r->dat.num;

		if (!boundscheck(wk, n->r, l_id, &i)) {
			return false;
		}

		return obj_array_index(wk, l_id, i, obj);
	}
	case obj_dict: {
		if (!typecheck(wk, n->r, r_id, obj_string)) {
			return false;
		}

		bool found;
		if (!obj_dict_index(wk, l_id, r_id, obj, &found)) {
			return false;
		} else if (!found) {
			interp_error(wk, n->r, "key not in dictionary: '%s'", wk_objstr(wk, r_id));
			return false;
		}
		return true;
	}
	case obj_custom_target: {
		if (!typecheck(wk, n->r, r_id, obj_number)) {
			return false;
		}

		int64_t i = r->dat.num;

		if (!boundscheck(wk, n->r, l->dat.custom_target.output, &i)) {
			return false;
		}

		return obj_array_index(wk, l->dat.custom_target.output, i, obj);
	}
	default:
		interp_error(wk, n->r, "index unsupported for %s", obj_type_to_s(l->type));
		return false;
	}
}

static bool
interp_chained(struct workspace *wk, uint32_t node_id, uint32_t l_id, uint32_t *obj)
{
	struct node *n = get_node(wk->ast, node_id);

	switch (n->type) {
	case node_method:
		return interp_method(wk, node_id, l_id, obj);
	case node_index:
		return interp_index(wk, n, l_id, obj);
	default:
		assert(false && "unreachable");
		break;
	}

	return false;
}
static bool
interp_u_minus(struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t l_id;

	if (!interp_node(wk, n->l, &l_id)) {
		return false;
	} else if (!typecheck(wk, n->l, l_id, obj_number)) {
		return false;
	}


	struct obj *num = make_obj(wk, obj, obj_number);
	num->dat.num = -get_obj(wk, l_id)->dat.num;

	return true;
}


static bool
interp_arithmetic(struct workspace *wk, uint32_t n_id, uint32_t *obj_id)
{
	uint32_t l_id, r_id;
	struct node *n = get_node(wk->ast, n_id);

	if (!interp_node(wk, n->l, &l_id)
	    || !interp_node(wk, n->r, &r_id)) {
		return false;
	}

	switch (get_obj(wk, l_id)->type) {
	case obj_string: {
		uint32_t res;

		if (!typecheck(wk, n->r, r_id, obj_string)) {
			return false;
		}

		switch ((enum arithmetic_type)n->subtype) {
		case arith_add:
			res = wk_str_pushf(wk, "%s%s",
				wk_objstr(wk, l_id),
				wk_objstr(wk, r_id));
			break;
		case arith_div: {
			char buf[PATH_MAX];
			if (!path_join(buf, PATH_MAX, wk_objstr(wk, l_id), wk_objstr(wk, r_id))) {
				return false;
			}

			res = wk_str_push(wk, buf);
			break;
		}
		default:
			goto err1;
		}

		make_obj(wk, obj_id, obj_string)->dat.str = res;
		break;
	}
	case obj_number: {
		int64_t res, l, r;

		if (!typecheck(wk, n->r, r_id, obj_number)) {
			return false;
		}

		l = get_obj(wk, l_id)->dat.num;
		r = get_obj(wk, r_id)->dat.num;

		switch ((enum arithmetic_type)n->subtype) {
		case arith_add:
			res = l + r;
			break;
		case arith_div:
			if (!r) {
				interp_error(wk, n->r, "divide by 0");
				return false;
			}
			res = l / r;
			break;
		case arith_sub:
			res = l - r;
			break;
		case arith_mod:
			if (!r) {
				interp_error(wk, n->r, "divide by 0");
				return false;
			}
			res = l % r;
			break;
		case arith_mul:
			res = l * r;
			break;
		}

		make_obj(wk, obj_id, obj_number)->dat.num = res;
		break;
	}
	case obj_array: {
		switch ((enum arithmetic_type)n->subtype) {
		case arith_add:
			if (!obj_array_dup(wk, l_id, obj_id)) {
				return false;
			}

			if (get_obj(wk, r_id)->type == obj_array) {
				obj_array_extend(wk, *obj_id, r_id);
			} else {
				obj_array_push(wk, *obj_id, r_id);
			}
			return true;
		default:
			goto err1;
		}
	}
	default:
		goto err1;
	}

	return true;
err1:
	assert(n->subtype < 5);
	interp_error(wk, n_id, "%s does not support %c", obj_type_to_s(get_obj(wk, l_id)->type), "+-%*/"[n->subtype]);
	return false;
}

static bool
interp_assign(struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t rhs;

	if (!interp_node(wk, n->r, &rhs)) {
		return false;
	}

	hash_set(&current_project(wk)->scope, get_node(wk->ast, n->l)->dat.s, rhs);
	return true;
}

static bool
interp_array(struct workspace *wk, uint32_t n_id, uint32_t *obj)
{
	uint32_t l, r;

	struct node *n = get_node(wk->ast, n_id);

	if (n->type == node_empty) {
		struct obj *arr = make_obj(wk, obj, obj_array);
		arr->dat.arr.len = 0;
		arr->dat.arr.tail = *obj;
		return true;
	}

	if (n->subtype == arg_kwarg) {
		interp_error(wk, n->l, "kwarg not valid in array constructor");
		return false;
	}

	bool have_c = n->chflg & node_child_c && get_node(wk->ast, n->c)->type != node_empty;

	if (!interp_node(wk, n->l, &l)) {
		return false;
	}

	if (have_c) {
		if (!interp_array(wk, n->c, &r)) {
			return false;
		}
	}

	struct obj *arr = make_obj(wk, obj, obj_array);
	arr->dat.arr.l = l;

	if ((arr->dat.arr.have_r = have_c)) {
		struct obj *arr_r = get_obj(wk, r);
		assert(arr_r->type == obj_array);

		arr->dat.arr.len = arr_r->dat.arr.len + 1;
		arr->dat.arr.tail = arr_r->dat.arr.tail;
		arr->dat.arr.r = r;
	} else {
		arr->dat.arr.len = 1;
		arr->dat.arr.tail = *obj;
	}

	return true;
}

static bool
interp_dict(struct workspace *wk, uint32_t n_id, uint32_t *obj)
{
	uint32_t key, value, tail;

	struct node *n = get_node(wk->ast, n_id);

	if (n->type == node_empty) {
		struct obj *dict = make_obj(wk, obj, obj_dict);
		dict->dat.dict.len = 0;
		dict->dat.dict.tail = *obj;
		return true;
	}

	assert(n->type == node_argument);

	if (n->subtype != arg_kwarg) {
		interp_error(wk, n->l, "non-kwarg not valid in dict constructor");
		return false;
	}

	bool have_c = n->chflg & node_child_c && get_node(wk->ast, n->c)->type != node_empty;

	if (!interp_node(wk, n->l, &key)) {
		return false;
	}

	if (!typecheck(wk, n->l, key, obj_string)) {
		return false;
	}

	if (!interp_node(wk, n->r, &value)) {
		return false;
	}

	if (have_c) {
		if (!interp_dict(wk, n->c, &tail)) {
			return false;
		}
	}

	struct obj *dict = make_obj(wk, obj, obj_dict);
	dict->dat.dict.key = key;
	dict->dat.dict.l = value;

	if ((dict->dat.dict.have_r = have_c)) {
		struct obj *dict_r = get_obj(wk, tail);
		assert(dict_r->type == obj_dict);

		bool res;
		if (!obj_dict_in(wk, key, tail, &res)) {
			assert(false && "this shouldn't fail");
			return false;
		} else if (res) {
			interp_error(wk, n->l, "key '%s' is duplicated", wk_objstr(wk, key));
			return false;
		}

		dict->dat.dict.len = dict_r->dat.dict.len + 1;
		dict->dat.dict.tail = dict_r->dat.dict.tail;
		dict->dat.dict.r = tail;
	} else {
		dict->dat.dict.len = 1;
		dict->dat.dict.tail = *obj;
	}

	return true;
}

static bool
interp_block(struct workspace *wk, struct node *n, uint32_t *obj)
{
	bool have_r = n->chflg & node_child_r
		      && get_node(wk->ast, n->r)->type != node_empty;

	assert(n->type == node_block);

	uint32_t obj_l, obj_r; // these return values are disregarded

	if (!interp_node(wk, n->l, &obj_l)) {
		return false;
	}

	if (have_r && get_node(wk->ast, n->r)->type != node_empty) {
		if (!interp_node(wk, n->r, &obj_r)) {
			return false;
		}

		*obj = obj_r;
	} else {
		*obj = obj_l;
	}

	return true;
}

static bool
interp_not(struct workspace *wk, struct node *n, uint32_t *obj_id)
{
	uint32_t obj_l_id;
	struct obj *obj;

	if (!interp_node(wk, n->l, &obj_l_id)) {
		return false;
	} else if (!typecheck(wk, n->l, obj_l_id, obj_bool)) {
		return false;
	}

	obj = make_obj(wk, obj_id, obj_bool);
	obj->dat.boolean = !get_obj(wk, obj_l_id)->dat.boolean;
	return true;
}

static bool
interp_andor(struct workspace *wk, struct node *n, uint32_t *obj_id)
{
	uint32_t obj_l_id, obj_r_id;
	struct obj *obj;

	if (!interp_node(wk, n->l, &obj_l_id)) {
		return false;
	} else if (!typecheck(wk, n->l, obj_l_id, obj_bool)) {
		return false;
	}

	if (n->type == node_and && !get_obj(wk, obj_l_id)->dat.boolean) {
		obj = make_obj(wk, obj_id, obj_bool);
		obj->dat.boolean = false;
		return true;
	} else if (n->type == node_or && get_obj(wk, obj_l_id)->dat.boolean) {
		obj = make_obj(wk, obj_id, obj_bool);
		obj->dat.boolean = true;
		return true;
	}

	if (!interp_node(wk, n->r, &obj_r_id)) {
		return false;
	} else if (!typecheck(wk, n->r, obj_r_id, obj_bool)) {
		return false;
	}

	obj = make_obj(wk, obj_id, obj_bool);
	obj->dat.boolean = get_obj(wk, obj_r_id)->dat.boolean;
	return true;
}

static bool
interp_comparison(struct workspace *wk, struct node *n, uint32_t *obj_id)
{
	bool res;
	uint32_t obj_l_id, obj_r_id;
	struct obj *obj;

	if (!interp_node(wk, n->l, &obj_l_id)) {
		return false;
	} else if (!interp_node(wk, n->r, &obj_r_id)) {
		return false;
	}

	switch ((enum comparison_type)n->subtype) {
	case comp_equal:
		res = obj_equal(wk, obj_l_id, obj_r_id);
		break;
	case comp_nequal:
		res = !obj_equal(wk, obj_l_id, obj_r_id);
		break;

	case comp_in:
	case comp_not_in:
		switch (get_obj(wk, obj_r_id)->type) {
		case obj_array:
			if (!obj_array_in(wk, obj_l_id, obj_r_id, &res)) {
				return false;
			}
			break;
		case obj_dict:
			if (!typecheck(wk, n->l, obj_l_id, obj_string)) {
				return false;
			}

			if (!obj_dict_in(wk, obj_l_id, obj_r_id, &res)) {
				return false;
			}
			break;
		default:
			interp_error(wk, n->r, "'in' not supported for %s", obj_type_to_s(get_obj(wk, obj_r_id)->type));
			return false;
		}

		if (n->subtype == comp_not_in) {
			res = !res;
		}
		break;

	case comp_lt:
	case comp_le:
	case comp_gt:
	case comp_ge: {
		if (!typecheck(wk, n->l, obj_l_id, obj_number)
		    || !typecheck(wk, n->r, obj_r_id, obj_number)) {
			return false;
		}

		int64_t a = get_obj(wk, obj_l_id)->dat.num,
			b = get_obj(wk, obj_r_id)->dat.num;

		switch (n->subtype) {
		case comp_lt:
			res = a < b;
			break;
		case comp_le:
			res = a <= b;
			break;
		case comp_gt:
			res = a > b;
			break;
		case comp_ge:
			res = a >= b;
			break;
		default: assert(false && "unreachable"); res = false;
		}
		break;
	}
	}

	obj = make_obj(wk, obj_id, obj_bool);
	obj->dat.boolean = res;
	return true;
}

static bool
interp_if(struct workspace *wk, struct node *n, uint32_t *obj)
{
	bool cond;
	uint32_t res_id;

	switch ((enum if_type)n->subtype) {
	case if_normal: {
		uint32_t cond_id;
		if (!interp_node(wk, n->l, &cond_id)) {
			return false;
		} else if (!typecheck(wk, n->l, cond_id, obj_bool)) {
			return false;
		}

		struct obj *cond_obj = get_obj(wk, cond_id);
		cond = cond_obj->dat.boolean;
		break;
	}
	case if_else:
		cond = true;
		break;
	}

	if (cond) {
		if (!interp_node(wk, n->r, &res_id)) {
			return false;
		}
	} else if (n->chflg & node_child_c) {
		if (!interp_node(wk, n->c, &res_id)) {
			return false;
		}
	} else {
		res_id = 0;
	}

	*obj = res_id;
	return true;
}

struct interp_foreach_ctx {
	const char *id1, *id2;
	uint32_t block_node;
};

static enum iteration_result
interp_foreach_common(struct workspace *wk, struct interp_foreach_ctx *ctx)
{
	uint32_t block_result;

	if (get_node(wk->ast, ctx->block_node)->type == node_empty) {
		return ir_done;
	}

	if (!interp_block(wk, get_node(wk->ast, ctx->block_node), &block_result)) {
		return ir_err;
	}

	switch (wk->loop_ctl) {
	case loop_continuing:
		wk->loop_ctl = loop_norm;
		break;
	case loop_breaking:
		wk->loop_ctl = loop_norm;
		return ir_done;
	case loop_norm:
		break;
	}

	return ir_cont;
}

static enum iteration_result
interp_foreach_dict_iter(struct workspace *wk, void *_ctx, uint32_t k_id, uint32_t v_id)
{
	struct interp_foreach_ctx *ctx = _ctx;

	hash_set(&current_project(wk)->scope, ctx->id1, k_id);
	hash_set(&current_project(wk)->scope, ctx->id2, v_id);

	return interp_foreach_common(wk, ctx);
}

static enum iteration_result
interp_foreach_arr_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct interp_foreach_ctx *ctx = _ctx;

	hash_set(&current_project(wk)->scope, ctx->id1, v_id);

	return interp_foreach_common(wk, ctx);
}

static bool
interp_foreach(struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t iterable;
	bool ret;

	if (!interp_node(wk, n->r, &iterable)) {
		return false;
	}

	struct node *args = get_node(wk->ast, n->l);

	switch (get_obj(wk, iterable)->type) {
	case obj_array: {
		if (args->chflg & node_child_r) {
			interp_error(wk, n->l, "array foreach needs exactly one variable to set");
			return false;
		}

		struct interp_foreach_ctx ctx = {
			.id1 = get_node(wk->ast, args->l)->dat.s,
			.block_node = n->c,
		};

		++wk->loop_depth;
		wk->loop_ctl = loop_norm;
		ret = obj_array_foreach(wk, iterable, &ctx, interp_foreach_arr_iter);
		--wk->loop_depth;

		break;
	}
	case obj_dict: {
		if (!(args->chflg & node_child_r)) {
			interp_error(wk, n->l, "dict foreach needs exactly two variables to set");
			return false;
		}

		assert(get_node(wk->ast, get_node(wk->ast, args->r)->type == node_foreach_args));

		struct interp_foreach_ctx ctx = {
			.id1 = get_node(wk->ast, args->l)->dat.s,
			.id2 = get_node(wk->ast, get_node(wk->ast, args->r)->l)->dat.s,
			.block_node = n->c,
		};

		++wk->loop_depth;
		wk->loop_ctl = loop_norm;
		ret = obj_dict_foreach(wk, iterable, &ctx, interp_foreach_dict_iter);
		--wk->loop_depth;

		break;
	}
	default:
		interp_error(wk, n->r, "%s is not iterable", obj_type_to_s(get_obj(wk, iterable)->type));
		return false;
	}

	return ret;
}

static bool
interp_func(struct workspace *wk, uint32_t n_id, uint32_t *obj)
{
	struct node *n = get_node(wk->ast, n_id);
	uint32_t res_id = 0;

	if (!builtin_run(wk, false, 0, n_id, &res_id)) {
		return false;
	}

	if (n->chflg & node_child_d) {
		return interp_chained(wk, n->d, res_id, obj);
	} else {
		*obj = res_id;
		return true;
	}
}

bool
interp_node(struct workspace *wk, uint32_t n_id, uint32_t *obj_id)
{
	static const uint32_t maximum_stack_depth = 1000;

	++wk->stack_depth;
	if (wk->stack_depth > maximum_stack_depth) {
		interp_error(wk, n_id, "stack too deep");
		--wk->stack_depth;
		return false;
	}

	bool ret;
	struct obj *obj;
	*obj_id = 0;

	struct node *n = get_node(wk->ast, n_id);

	/* L(log_interp, "%s", node_to_s(n)); */
	if (wk->loop_ctl) {
		--wk->stack_depth;
		return true;
	}

	switch (n->type) {
	/* literals */
	case node_bool:
		obj = make_obj(wk, obj_id, obj_bool);
		obj->dat.boolean = n->subtype;
		ret = true;
		break;
	case node_format_string: // TODO fallthrough for now :)
	case node_string:
		obj = make_obj(wk, obj_id, obj_string);
		obj->dat.str = wk_str_push(wk, n->dat.s);
		ret = true;
		break;
	case node_array:
		ret = interp_array(wk, n->l, obj_id);
		break;
	case node_dict:
		ret = interp_dict(wk, n->l, obj_id);
		break;
	case node_id:
		if (!get_obj_id(wk, n->dat.s, obj_id, wk->cur_project)) {
			interp_error(wk, n_id, "undefined object");
			ret = false;
			break;
		}
		ret = true;
		break;
	case node_number:
		obj = make_obj(wk, obj_id, obj_number);
		obj->dat.num = n->dat.n;
		ret = true;
		break;

	/* control flow */
	case node_block:
		ret = interp_block(wk, n, obj_id);
		break;
	case node_if:
		ret = interp_if(wk, n, obj_id);
		break;
	case node_foreach:
		ret = interp_foreach(wk, n, obj_id);
		break;
	case node_continue:
		if (!wk->loop_depth) {
			LOG_W(log_interp, "continue outside loop");
			ret = false;
			break;
		}
		wk->loop_ctl = loop_continuing;
		ret = true;
		break;
	case node_break:
		if (!wk->loop_depth) {
			LOG_W(log_interp, "break outside loop");
			ret = false;
			break;
		}
		wk->loop_ctl = loop_breaking;
		ret = true;
		break;

	/* functions */
	case node_function:
		ret = interp_func(wk, n_id, obj_id);
		break;
	case node_method:
	case node_index: {
		uint32_t l_id;
		assert(n->chflg & node_child_l);

		if (!interp_node(wk, n->l, &l_id)) {
			ret = false;
			break;
		}

		ret = interp_chained(wk, n_id, l_id, obj_id);
		break;
	}

	/* assignment */
	case node_assignment:
		ret = interp_assign(wk, n, obj_id);
		break;

	/* comparison stuff */
	case node_not:
		ret = interp_not(wk, n, obj_id);
		break;
	case node_and:
	case node_or:
		ret = interp_andor(wk, n, obj_id);
		break;
	case node_comparison:
		ret = interp_comparison(wk, n, obj_id);
		break;
	case node_ternary:
		LOG_W(log_misc, "ternary not yet implemented");
		ret = false;
		break;

	/* math */
	case node_u_minus:
		ret = interp_u_minus(wk, n, obj_id);
		break;
	case node_arithmetic:
		ret = interp_arithmetic(wk, n_id, obj_id);
		break;

	/* handled in other places */
	case node_foreach_args:
	case node_argument:
		assert(false && "unreachable");
		break;

	case node_empty:
		ret = true;
		break;

	/* never valid */
	case node_null:
		LOG_W(log_interp, "bug in the interpreter: encountered null node");
		ret = false;
		break;
	}

	--wk->stack_depth;
	return ret;
}
