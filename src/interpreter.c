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
#include "workspace.h"

__attribute__ ((format(printf, 3, 4)))
void
interp_error(struct workspace *wk, uint32_t n_id, const char *fmt, ...)
{
	struct node *n = get_node(wk->ast, n_id);

	va_list args;
	va_start(args, fmt);
	error_message(wk->ast->toks->src_path, n->tok->line, n->tok->col, fmt, args);
	va_end(args);
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

static bool
interp_method(struct workspace *wk, uint32_t node_id, uint32_t *obj)
{
	uint32_t recvr_id;

	struct node *n = get_node(wk->ast, node_id);

	if (!interp_node(wk, n->l, &recvr_id)) {
		return false;
	}

	return builtin_run(wk, recvr_id, node_id, obj);
}

static bool
interp_index(struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t l_id, r_id;

	if (!interp_node(wk, n->l, &l_id)) {
		return false;
	} else if (!interp_node(wk, n->r, &r_id)) {
		return false;
	}

	struct obj *l = get_obj(wk, l_id), *r = get_obj(wk, r_id);
	switch (l->type) {
	case obj_array:
		if (!typecheck(wk, n->r, r_id, obj_number)) {
			return false;
		}

		int64_t i = r->dat.num;

		struct obj *arr = get_obj(wk, l_id);
		if (labs(i) >= arr->dat.arr.len) {
			interp_error(wk, n->r, "index %ld out of bounds", i);
			return false;
		}

		if (i < 0) {
			i += arr->dat.n;
		}

		return obj_array_index(wk, l_id, r->dat.num, obj);
	default:
		interp_error(wk, n->l, "index unsupported for %s", obj_type_to_s(l->type));
		return false;
	}
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
	struct obj *obj, *l, *r;

	if (!interp_node(wk, n->l, &l_id)
	    || !interp_node(wk, n->r, &r_id)) {
		return false;
	}

	l = get_obj(wk, l_id);
	r = get_obj(wk, r_id);

	if (l->type != r->type) {
		interp_error(wk, n_id, "arithmetic operands (%s and %s) must match in type",
			obj_type_to_s(l->type),
			obj_type_to_s(r->type));
		return false;
	}

	switch (get_obj(wk, l_id)->type) {
	case obj_string: {
		uint32_t res;

		switch ((enum arithmetic_type)n->data) {
		case arith_add:
			res = wk_str_pushf(wk, "%s%s",
				wk_str(wk, l->dat.str),
				wk_str(wk, r->dat.str));
			break;
		default:
			goto err1;
		}

		obj = make_obj(wk, obj_id, obj_string);
		obj->dat.str = res;
		break;
	}
	case obj_number: {
		int64_t res;

		switch ((enum arithmetic_type)n->data) {
		case arith_add:
			res = l->dat.num + r->dat.num;
			break;
		case arith_div:
			res = l->dat.num / r->dat.num;
			break;
		case arith_sub:
			res = l->dat.num - r->dat.num;
			break;
		case arith_mod:
			res = l->dat.num % r->dat.num;
			break;
		case arith_mul:
			res = l->dat.num * r->dat.num;
			break;
		}

		obj = make_obj(wk, obj_id, obj_number);
		obj->dat.num = res;
		break;
	}
	default:
		goto err1;
	}

	return true;
err1:
	assert(n->data < 5);
	interp_error(wk, n_id, "%s does not support %c", obj_type_to_s(get_obj(wk, l_id)->type), "+/-%*"[n->data]);
	return false;
}

static bool
interp_assign(struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t rhs;
	if (!interp_node(wk, n->r, &rhs)) {
		return false;
	}

	// TODO check if we are overwriting?
	hash_set(&current_project(wk)->scope, get_node(wk->ast, n->l)->tok->dat.s, rhs);

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

	if (n->data == arg_kwarg) {
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
interp_block(struct workspace *wk, struct node *n, uint32_t *obj)
{
	bool have_r = n->chflg & node_child_r
		      && get_node(wk->ast, n->r)->type != node_empty;

	uint32_t obj_l, obj_r; // these return values are disregarded

	if (!interp_node(wk, n->l, &obj_l)) {
		return false;
	}

	if (have_r) {
		if (!interp_node(wk, n->r, &obj_r)) {
			return false;
		}
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

	switch ((enum comparison_type)n->data) {
	case comp_equal:
		res = obj_equal(wk, obj_l_id, obj_r_id);
		break;
	case comp_nequal:
		res = !obj_equal(wk, obj_l_id, obj_r_id);
		break;

	case comp_in:
	case comp_not_in:
		if (!obj_array_in(wk, obj_l_id, obj_r_id, &res)) {
			return false;
		}

		if (n->data == comp_not_in) {
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

		switch (n->data) {
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

	switch ((enum if_type)n->data) {
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
	}

	return true;
}

struct interp_foreach_ctx {
	const char *arg1;
	uint32_t block;
};

static enum iteration_result
interp_foreach_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	uint32_t block_result;
	struct interp_foreach_ctx *ctx = _ctx;

	hash_set(&current_project(wk)->scope, ctx->arg1, v_id);

	if (!interp_block(wk, get_node(wk->ast, ctx->block), &block_result)) {
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

static bool
interp_foreach(struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t arr;

	if (!interp_node(wk, n->r, &arr)) {
		return false;
	} else if (!typecheck(wk, n->r, arr, obj_array)) {
		// TODO: support dict
		return false;
	}

	struct interp_foreach_ctx ctx = {
		.arg1 = get_node(wk->ast, get_node(wk->ast, n->l)->l)->tok->dat.s,
		.block = n->c,
	};


	++wk->loop_depth;
	wk->loop_ctl = loop_norm;

	obj_array_foreach(wk, arr, &ctx, interp_foreach_iter);

	--wk->loop_depth;

	return true;
}

bool
interp_node(struct workspace *wk, uint32_t n_id, uint32_t *obj_id)
{
	struct obj *obj;
	*obj_id = 0;

	struct node *n = get_node(wk->ast, n_id);

	/* L(log_interp, "%s", node_to_s(n)); */
	if (wk->loop_ctl) {
		return true;
	}

	switch (n->type) {
	/* literals */
	case node_bool:
		obj = make_obj(wk, obj_id, obj_bool);
		obj->dat.boolean = n->data;
		return true;
	case node_format_string: // TODO :)
	case node_string:
		obj = make_obj(wk, obj_id, obj_string);
		obj->dat.str = wk_str_push(wk, n->tok->dat.s);
		return true;
	case node_array:
		return interp_array(wk, n->l, obj_id);
	case node_id:
		if (!get_obj_id(wk, n->tok->dat.s, obj_id, wk->cur_project)) {
			interp_error(wk, n_id, "undefined object");
			return false;
		}
		return true;
	case node_number:
		obj = make_obj(wk, obj_id, obj_number);
		obj->dat.num = n->tok->dat.n;
		return true;
	case node_dict: break;

	/* control flow */
	case node_block:
		return interp_block(wk, n, obj_id);
	case node_if:
		return interp_if(wk, n, obj_id);
	case node_foreach:
		return interp_foreach(wk, n, obj_id);
	case node_continue:
		if (!wk->loop_depth) {
			LOG_W(log_interp, "continue outside loop");
			return false;
		}
		wk->loop_ctl = loop_continuing;
		break;
	case node_break:
		if (!wk->loop_depth) {
			LOG_W(log_interp, "break outside loop");
			return false;
		}
		wk->loop_ctl = loop_breaking;
		break;

	/* functions */
	case node_function:
		return builtin_run(wk, 0, n_id, obj_id);
	case node_method:
		return interp_method(wk, n_id, obj_id);
	case node_index:
		return interp_index(wk, n, obj_id);

	/* assignment */
	case node_assignment:
		return interp_assign(wk, n, obj_id);

	/* comparison stuff */
	case node_not:
		return interp_not(wk, n, obj_id);
	case node_and:
	case node_or:
		return interp_andor(wk, n, obj_id);
	case node_comparison:
		return interp_comparison(wk, n, obj_id);
	case node_ternary: break;

	/* math */
	case node_u_minus:
		return interp_u_minus(wk, n, obj_id);
	case node_arithmetic:
		return interp_arithmetic(wk, n_id, obj_id);

	/* handled in other places */
	case node_foreach_args:
	case node_argument:
		assert(false && "unreachable");
		break;

	case node_empty: return true;

	/* never valid */
	case node_null:
		LOG_W(log_interp, "bug in the interpreter: encountered null node");
		return false;
	}

	return true;
	/* assert(false && "unreachable"); */
	/* return false; */
}

bool
interpreter_interpret(struct workspace *wk)
{
	uint32_t obj;

	return interp_node(wk, wk->ast->root, &obj);
}
