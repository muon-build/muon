#include "posix.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "functions/common.h"
#include "hash.h"
#include "interpreter.h"
#include "log.h"
#include "parser.h"
#include "workspace.h"

bool
typecheck(struct obj *o, enum obj_type type)
{
	if (type != obj_any && o->type != type) {
		LOG_W(log_interp, "expected type %s, got %s", obj_type_to_s(type), obj_type_to_s(o->type));
		return false;
	}

	return true;
}

static bool
interp_method(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t recvr_id;

	if (!interp_node(ast, wk, get_node(ast, n->l), &recvr_id)) {
		return false;
	}

	return builtin_run(ast, wk, recvr_id, n, obj);
}

static bool
interp_index(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t l_id, r_id;

	if (!interp_node(ast, wk, get_node(ast, n->l), &l_id)) {
		return false;
	} else if (!interp_node(ast, wk, get_node(ast, n->r), &r_id)) {
		return false;
	}

	struct obj *l = get_obj(wk, l_id), *r = get_obj(wk, r_id);
	switch (l->type) {
	case obj_array:
		if (!typecheck(r, obj_number)) {
			return false;
		}

		return obj_array_index(wk, l_id, r->dat.num, obj);
	default:
		LOG_W(log_interp, "index unsupported for %s", obj_type_to_s(l->type));
		return false;
	}
}

static bool
interp_u_minus(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t l_id;

	if (!interp_node(ast, wk, get_node(ast, n->l), &l_id)) {
		return false;
	} else if (!typecheck(get_obj(wk, l_id), obj_number)) {
		return false;
	}


	struct obj *num = make_obj(wk, obj, obj_number);
	num->dat.num = -get_obj(wk, l_id)->dat.num;

	return true;
}


static bool
interp_arithmetic(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj_id)
{
	uint32_t l_id, r_id;
	struct obj *obj, *l, *r;

	if (!interp_node(ast, wk, get_node(ast, n->l), &l_id)
	    || !interp_node(ast, wk, get_node(ast, n->r), &r_id)) {
		return false;
	}

	l = get_obj(wk, l_id);
	r = get_obj(wk, r_id);

	if (l->type != r->type) {
		LOG_W(log_interp, "arithmetic operands must match in type");
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
	LOG_W(log_interp, "invalid operator for %s", obj_type_to_s(get_obj(wk, l_id)->type));
	return false;
}

static bool
interp_assign(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t rhs;
	if (!interp_node(ast, wk, get_node(ast, n->r), &rhs)) {
		return false;
	}

	// TODO check if we are overwriting?
	hash_set(&current_project(wk)->scope, get_node(ast, n->l)->tok->dat.s, rhs);

	return true;
}

static bool
interp_array(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t l, r;
	bool have_c = n->chflg & node_child_c && get_node(ast, n->c)->type != node_empty;

	if (n->data == arg_kwarg) {
		LOG_W(log_interp, "invalid kwarg in array constructor");
		return false;
	}

	if (!interp_node(ast, wk, get_node(ast, n->l), &l)) {
		return false;
	}

	if (have_c) {
		if (!interp_array(ast, wk, get_node(ast, n->c), &r)) {
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
interp_block(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	bool have_r = n->chflg & node_child_r
		      && get_node(ast, n->r)->type != node_empty;

	uint32_t obj_l, obj_r; // these return values are disregarded

	if (!interp_node(ast, wk, get_node(ast, n->l), &obj_l)) {
		return false;
	}

	if (have_r) {
		if (!interp_node(ast, wk, get_node(ast, n->r), &obj_r)) {
			return false;
		}
	}

	return true;
}

static bool
interp_not(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj_id)
{
	uint32_t obj_l_id;
	struct obj *obj;

	if (!interp_node(ast, wk, get_node(ast, n->l), &obj_l_id)) {
		return false;
	} else if (!typecheck(get_obj(wk, obj_l_id), obj_bool)) {
		return false;
	}

	obj = make_obj(wk, obj_id, obj_bool);
	obj->dat.boolean = !get_obj(wk, obj_l_id)->dat.boolean;
	return true;
}

static bool
interp_andor(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj_id)
{
	uint32_t obj_l_id, obj_r_id;
	struct obj *obj;

	if (!interp_node(ast, wk, get_node(ast, n->l), &obj_l_id)) {
		return false;
	} else if (!typecheck(get_obj(wk, obj_l_id), obj_bool)) {
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

	if (!interp_node(ast, wk, get_node(ast, n->r), &obj_r_id)) {
		return false;
	} else if (!typecheck(get_obj(wk, obj_r_id), obj_bool)) {
		return false;
	}

	obj = make_obj(wk, obj_id, obj_bool);
	obj->dat.boolean = get_obj(wk, obj_r_id)->dat.boolean;
	return true;
}

static bool
interp_comparison(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj_id)
{
	bool res;
	uint32_t obj_l_id, obj_r_id;
	struct obj *obj;

	if (!interp_node(ast, wk, get_node(ast, n->l), &obj_l_id)) {
		return false;
	} else if (!interp_node(ast, wk, get_node(ast, n->r), &obj_r_id)) {
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
		if (!typecheck(get_obj(wk, obj_l_id), obj_number)
		    || !typecheck(get_obj(wk, obj_r_id), obj_number)) {
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
interp_if(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	bool cond;
	uint32_t res_id;

	switch ((enum if_type)n->data) {
	case if_normal: {
		uint32_t cond_id;
		if (!interp_node(ast, wk, get_node(ast, n->l), &cond_id)) {
			return false;
		}

		struct obj *cond_obj = get_obj(wk, cond_id);

		if (!typecheck(cond_obj, obj_bool)) {
			return false;
		}

		cond = cond_obj->dat.boolean;
		break;
	}
	case if_else:
		cond = true;
		break;
	}

	if (cond) {
		if (!interp_node(ast, wk, get_node(ast, n->r), &res_id)) {
			return false;
		}
	} else if (n->chflg & node_child_c) {
		if (!interp_node(ast, wk, get_node(ast, n->c), &res_id)) {
			return false;
		}
	}

	return true;
}

struct interp_foreach_ctx {
	const char *arg1;
	struct ast *ast;
	uint32_t block;
};

static enum iteration_result
interp_foreach_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	uint32_t block_result;
	struct interp_foreach_ctx *ctx = _ctx;

	hash_set(&current_project(wk)->scope, ctx->arg1, v_id);

	if (!interp_block(ctx->ast, wk, get_node(ctx->ast, ctx->block), &block_result)) {
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
interp_foreach(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t arr;

	if (!interp_node(ast, wk, get_node(ast, n->r), &arr)) {
		return false;
	} else if (!typecheck(get_obj(wk, arr), obj_array)) {
		// TODO: support dict
		return false;
	}

	struct interp_foreach_ctx ctx = {
		.arg1 = get_node(ast, get_node(ast, n->l)->l)->tok->dat.s,
		.block = n->c,
		.ast = ast,
	};


	++wk->loop_depth;
	wk->loop_ctl = loop_norm;

	obj_array_foreach(wk, arr, &ctx, interp_foreach_iter);

	--wk->loop_depth;

	return true;
}

bool
interp_node(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj_id)
{
	struct obj *obj;
	*obj_id = 0;

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
		return interp_array(ast, wk, get_node(ast, n->l), obj_id);
	case node_id:
		return get_obj_id(wk, n->tok->dat.s, obj_id, wk->cur_project);
	case node_number:
		obj = make_obj(wk, obj_id, obj_number);
		obj->dat.num = n->tok->dat.n;
		return true;
	case node_dict: break;

	/* control flow */
	case node_block:
		return interp_block(ast, wk, n, obj_id);
	case node_if:
		return interp_if(ast, wk, n, obj_id);
	case node_foreach:
		return interp_foreach(ast, wk, n, obj_id);
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
		return builtin_run(ast, wk, 0, n, obj_id);
	case node_method:
		return interp_method(ast, wk, n, obj_id);
	case node_index:
		return interp_index(ast, wk, n, obj_id);

	/* assignment */
	case node_assignment:
		return interp_assign(ast, wk, n, obj_id);

	/* comparison stuff */
	case node_not:
		return interp_not(ast, wk, n, obj_id);
	case node_and:
	case node_or:
		return interp_andor(ast, wk, n, obj_id);
	case node_comparison:
		return interp_comparison(ast, wk, n, obj_id);
	case node_ternary: break;

	/* math */
	case node_u_minus:
		return interp_u_minus(ast, wk, n, obj_id);
	case node_arithmetic:
		return interp_arithmetic(ast, wk, n, obj_id);

	/* handled in other places */
	case node_foreach_args:
	case node_argument:
		assert(false && "unreachable");
		break;

	case node_empty: return true;

	/* never valid */
	case node_null:
		assert(false && "unreachable");
		break;
	}

	return true;
	/* assert(false && "unreachable"); */
	/* return false; */
}

bool
interpret(struct ast *ast, struct workspace *wk)
{
	uint32_t obj;

	return interp_node(ast, wk, get_node(ast, ast->root), &obj);
}
