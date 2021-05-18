#include "posix.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "builtin.h"
#include "hash.h"
#include "interpreter.h"
#include "log.h"
#include "parser.h"

static bool
interp_function(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	return builtin_run(ast, wk, NULL, n, obj);
}

static bool
interp_method(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	struct obj *recvr;
	uint32_t recvr_id;

	if (!interp_node(ast, wk, get_node(ast, n->l), &recvr_id)) {
		return false;
	}

	recvr = get_obj(wk, recvr_id);

	return builtin_run(ast, wk, recvr, n, obj);
}

static bool
interp_id(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	return get_obj_id(wk, n->tok->data, obj);
}

static bool
interp_assign(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	uint32_t rhs;
	if (!interp_node(ast, wk, get_node(ast, n->r), &rhs)) {
		return false;
	}

	// TODO check if we are overwriting?
	hash_set(&wk->obj_names, get_node(ast, n->l)->tok->data, rhs);

	return true;
}

static bool
interp_string(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	struct obj *str;
	str = make_obj(wk, obj, obj_string);
	str->dat.s = n->tok->data;
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

bool
interp_node(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	switch (n->type) {
	case node_function:
		return interp_function(ast, wk, n, obj);
	case node_method:
		return interp_method(ast, wk, n, obj);
	case node_assignment:
		return interp_assign(ast, wk, n, obj);
	case node_string:
		return interp_string(ast, wk, n, obj);
	case node_array:
		return interp_array(ast, wk, get_node(ast, n->l), obj);
	case node_id:
		return interp_id(ast, wk, n, obj);
	case node_bool: break;
	case node_number: break;
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
	case node_if: break;
	case node_if_clause: break;
	case node_u_minus: break;
	case node_ternary: break;
	}

	return true;
}

bool
interpret(struct ast *ast, struct workspace *wk)
{
	uint32_t i;
	for (i = 0; i < ast->ast.len; ++i) {
		uint32_t obj = UINT32_MAX;

		if (!interp_node(ast, wk, get_node(ast, *(uint32_t *)darr_get(&ast->ast, i)), &obj)) {
			return false;
		}
	}

	return true;
}
