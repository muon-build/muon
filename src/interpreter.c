#include "posix.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "builtin.h"
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

bool
interp_node(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj_id)
{
	struct obj *obj;
	*obj_id = 0;

	/* L(log_interp, "%s", node_to_s(n)); */

	switch (n->type) {
	case node_block:
		return interp_block(ast, wk, n, obj_id);
	case node_function:
		return builtin_run(ast, wk, 0, n, obj_id);
	case node_method:
		return interp_method(ast, wk, n, obj_id);
	case node_assignment:
		return interp_assign(ast, wk, n, obj_id);
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
	case node_if:
		return interp_if(ast, wk, n, obj_id);
	case node_bool:
		obj = make_obj(wk, obj_id, obj_bool);
		obj->dat.boolean = n->data;
		return true;
	case node_format_string: break;
	case node_continue: break;
	case node_break: break;
	case node_argument: break;
	case node_dict: break;
	case node_empty: break;
	case node_or: break;
	case node_and: break;
	case node_comparison: break;
	case node_arithmetic: break;
	case node_not: break;
	case node_index: break;
	case node_plus_assignment: break;
	case node_foreach_clause: break;
	case node_u_minus: break;
	case node_ternary: break;
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

	return true;
}
