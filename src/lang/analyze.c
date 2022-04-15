#include "posix.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "data/hash.h"
#include "error.h"
#include "functions/common.h"
#include "functions/modules.h"
#include "lang/analyze.h"
#include "lang/eval.h"
#include "lang/interpreter.h"
#include "lang/parser.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"

static bool analyze_error;

struct assignment {
	const char *name;
	obj o;
	bool accessed, default_var;
	uint32_t n_id;
};

struct scope_group {
	struct darr scopes;
};

struct {
	struct darr groups;
	struct darr base;
} assignment_scopes;

static const char *
inspect_typeinfo(struct workspace *wk, obj t)
{
	if (get_obj_type(wk, t) == obj_typeinfo) {
		struct obj_typeinfo *ti = get_obj_typeinfo(wk, t);

		return typechecking_type_to_s(wk, ti->type);
	} else {
		return obj_type_to_s(get_obj_type(wk, t));
	}
}

static obj
make_typeinfo(struct workspace *wk, uint32_t t, uint32_t sub_t)
{
	assert(t & obj_typechecking_type_tag);
	if (sub_t) {
		assert(sub_t & obj_typechecking_type_tag);
	}

	obj res;
	make_obj(wk, &res, obj_typeinfo);
	struct obj_typeinfo *type = get_obj_typeinfo(wk, res);
	type->type = t;
	type->subtype = sub_t;
	return res;
}

static uint32_t
obj_type_to_tc_type(enum obj_type t)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(typemap); ++i) {
		if (t == typemap[i].type) {
			return typemap[i].tc;
		}
	}

	UNREACHABLE_RETURN;
}

static void
merge_types(struct workspace *wk, struct obj_typeinfo *a, obj r)
{
	enum obj_type t = get_obj_type(wk, r);
	if (t == obj_typeinfo) {
		a->type |= get_obj_typeinfo(wk, r)->type;
	} else {
		a->type |= obj_type_to_tc_type(t);
	}
}

struct analyze_ctx {
	uint32_t expected;
	uint32_t found;
	const struct func_impl_name *found_func;
	struct obj_typeinfo ti;
	enum comparison_type comparison_type;
	obj l;
};

typedef void ((analyze_for_each_type_cb)(struct workspace *wk,
	struct analyze_ctx *ctx, uint32_t n_id, enum obj_type t, obj *res));

static void
analyze_for_each_type(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id,
	obj o, uint32_t typemask, analyze_for_each_type_cb cb, obj *res)
{
	uint32_t i;
	obj r = 0;

	uint32_t t;
	if ((t = get_obj_type(wk, o)) == obj_typeinfo) {
		t = get_obj_typeinfo(wk, o)->type;
	}

	if (t & obj_typechecking_type_tag) {
		struct obj_typeinfo res_t = { 0 };

		if (typemask) {
			t &= typemask;
		}

		for (i = 0; i < ARRAY_LEN(typemap); ++i) {
			if ((t & typemap[i].tc) == typemap[i].tc) {
				r = 0;
				cb(wk, ctx, n_id, typemap[i].type, &r);
				if (r) {
					merge_types(wk, &res_t, r);
				}
			}
		}

		make_obj(wk, res, obj_typeinfo);
		*get_obj_typeinfo(wk, *res) = res_t;
	} else {
		cb(wk, ctx, n_id, t, res);
	}
}

/*
 * Variable assignment and retrieval is handled with the following functions.
 * The additional complexity is required due to variables that are
 * conditionally assigned, e.g. assigned in an if-block, or foreach loop.
 *
 * When a conditional block is started, a "scope group" is created, and then
 * every branch of the if statement gets its own sub-scope.  At the end of the
 * if statement, all the sub scopes are merged (conflicting object types get
 * merged) into the parent scope, and the scope group is popped.
 */

static struct assignment *
assign_lookup_scope(const char *name, struct darr *scope)
{
	uint32_t i;
	for (i = 0; i < scope->len; ++i) {
		struct assignment *a = darr_get(scope, i);
		if (strcmp(a->name, name) == 0) {
			return a;
		}
	}

	return NULL;
}

static struct assignment *
assign_lookup(struct workspace *wk, const char *name)
{
	uint32_t i;
	uint32_t id;
	struct assignment *found = NULL;

	for (i = 0; i < assignment_scopes.groups.len; ++i) {
		struct scope_group *g = darr_get(&assignment_scopes.groups, i);
		if (!(g->scopes.len)) {
			continue;
		}

		struct darr *scope = darr_get(&g->scopes, g->scopes.len - 1);
		if ((found = assign_lookup_scope(name, scope))) {
			break;
		}
	}

	if (!found && wk->projects.len && get_obj_id(wk, name, &id, wk->cur_project)) {
		found = darr_get(&assignment_scopes.base, id);
	}

	return found;
}

static struct assignment *
scope_assign(struct workspace *wk, const char *name, obj o, uint32_t n_id)
{
	struct assignment *a;
	if ((a = assign_lookup(wk, name))) {
		a->o = o;
		return a;
	}

	struct darr *s;
	if (assignment_scopes.groups.len) {
		struct scope_group *g = darr_get(&assignment_scopes.groups, assignment_scopes.groups.len - 1);

		assert(g->scopes.len);
		s = darr_get(&g->scopes, g->scopes.len - 1);
	} else {
		s = &assignment_scopes.base;
	}

	darr_push(s, &(struct assignment) {
		.name = name,
		.o = o,
		.n_id = n_id,
	});

	if (!assignment_scopes.groups.len) {
		struct hash *scope;
		if (wk->projects.len) {
			scope = &current_project(wk)->scope;
		} else {
			scope = &wk->scope;
		}

		hash_set_str(scope, name, s->len - 1);
	}

	return darr_get(s, s->len - 1);
}

static void
push_scope_group_scope(void)
{
	assert(assignment_scopes.groups.len);
	struct scope_group *g = darr_get(&assignment_scopes.groups, assignment_scopes.groups.len - 1);
	darr_push(&g->scopes, &(struct darr) { 0 });
	struct darr *scope = darr_get(&g->scopes, g->scopes.len - 1);
	darr_init(scope, 256, sizeof(struct assignment));
}

static void
push_scope_group(void)
{
	struct scope_group g = { 0 };
	darr_init(&g.scopes, 4, sizeof(struct darr));
	darr_push(&assignment_scopes.groups, &g);
}

static void
pop_scope_group(struct workspace *wk)
{
	assert(assignment_scopes.groups.len);
	size_t idx = assignment_scopes.groups.len - 1;
	struct scope_group *g = darr_get(&assignment_scopes.groups, idx);
	darr_del(&assignment_scopes.groups, idx);

	struct darr *base = darr_get(&g->scopes, 0);

	uint32_t i, j;
	for (i = 1; i < g->scopes.len; ++i) {
		struct darr *scope = darr_get(&g->scopes, i);
		for (j = 0; j < scope->len; ++j) {
			struct assignment *old, *a = darr_get(scope, j);
			if ((old = assign_lookup_scope(a->name, base))) {
				if (get_obj_type(wk, old->o) != obj_typeinfo) {
					obj old_tc = make_typeinfo(wk, obj_type_to_tc_type(get_obj_type(wk, old->o)), 0);
					old->o = old_tc;
				}

				if (a->o) {
					merge_types(wk, get_obj_typeinfo(wk, old->o), a->o);
				}
			} else {
				darr_push(base, a);
			}
		}
	}

	for (i = 0; i < base->len; ++i) {
		struct assignment *a = darr_get(base, i),
				  *b = scope_assign(wk, a->name, a->o, a->n_id);

		b->accessed = a->accessed;
	}

	for (i = 0; i < g->scopes.len; ++i) {
		struct darr *scope = darr_get(&g->scopes, i);
		darr_destroy(scope);
	}
	darr_destroy(&g->scopes);
}

/*
 *-----------------------------------------------------------------------------
 */

static bool
analyze_function_call(struct workspace *wk, uint32_t n_id, uint32_t args_node, const struct func_impl_name *fi, obj rcvr, obj *res)
{
	bool ret = true;
	obj func_res;
	bool old_analyze_error = analyze_error;
	analyze_error = false;

	if (!analyze_function(wk, fi, args_node, rcvr, &func_res)
	    || analyze_error) {
		interp_error(wk, n_id, "in function %s", fi->name);
		ret = false;
	}

	analyze_error = old_analyze_error;

	if (func_res) {
		*res = func_res;
	} else if (fi->return_type) {
		*res = make_typeinfo(wk, fi->return_type, 0);
	}

	return ret;
}

static bool analyze_chained(struct workspace *wk, uint32_t n_id, obj l_id, obj *res);

static void
analyze_method(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id, enum obj_type rcvr_type, obj *res)
{
	struct node *n = get_node(wk->ast, n_id);

	const char *name = get_node(wk->ast, n->r)->dat.s;
	const struct func_impl_name *fi;

	if (rcvr_type == obj_module && get_obj_type(wk, ctx->l) != obj_typeinfo) {
		struct obj_module *m = get_obj_module(wk, ctx->l);
		enum module mod = m->module;
		if (!(fi = module_func_lookup(name, mod))) {
			return;
		}
	} else {
		const struct func_impl_name *impl_tbl = func_tbl[rcvr_type][wk->lang_mode];

		if (!impl_tbl) {
			return;
		}

		if (!(fi = func_lookup(impl_tbl, name))) {
			return;
		}
	}

	if (fi->return_type) {
		*res = make_typeinfo(wk, fi->return_type, 0);
	}

	++ctx->found;
	ctx->found_func = fi;

	return;
}

static void
analyze_index(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id, enum obj_type lhs, obj *res)
{
	switch (lhs) {
	case obj_disabler:
		*res = disabler_id;
		return;
	case obj_array: {
		ctx->expected |= tc_number;
		*res = make_typeinfo(wk, tc_any, 0);
		break;
	}
	case obj_dict: {
		ctx->expected |= tc_string;
		*res = make_typeinfo(wk, tc_any, 0);
		break;
	}
	case obj_custom_target: {
		ctx->expected |= tc_number;
		*res = make_typeinfo(wk, tc_file, 0);
		break;
	}
	case obj_string: {
		ctx->expected |= tc_number;
		*res = make_typeinfo(wk, tc_string, 0);
		break;
	}
	default:
		UNREACHABLE;
	}

	return;
}

static bool
analyze_chained(struct workspace *wk, uint32_t n_id, obj l_id, obj *res)
{
	bool ret = true;
	struct node *n = get_node(wk->ast, n_id);
	obj tmp = 0;

	switch (n->type) {
	case node_method: {
		struct analyze_ctx ctx = { .l = l_id };

		analyze_for_each_type(wk, &ctx, n_id, l_id, 0, analyze_method, &tmp);

		if (ctx.found == 1) {
			if (!analyze_function_call(wk, n_id, n->c, ctx.found_func, l_id, &tmp)) {
				ret = false;
			}
		} else if (ctx.found) {
			if (ctx.expected) {
				tmp = make_typeinfo(wk, ctx.expected, 0);
			}
		} else if (!ctx.found) {
			tmp = make_typeinfo(wk, tc_any, 0);
			interp_error(wk, n_id, "method %s not found on %s", get_node(wk->ast, n->r)->dat.s, inspect_typeinfo(wk, l_id));
			ret = false;
		}
		break;
	}
	case node_index: {
		obj r;

		if (!wk->interp_node(wk, n->r, &r)) {
			r = make_typeinfo(wk, tc_any, 0);
			ret = false;
		}

		struct analyze_ctx ctx = { 0 };
		const uint32_t tc_lhs = tc_string | tc_custom_target | tc_dict | tc_array;

		if (typecheck(wk, n->l, l_id, tc_lhs)) {
			analyze_for_each_type(wk, &ctx, n_id, l_id, tc_lhs, analyze_index, &tmp);

			if (typecheck(wk, n->r, r, ctx.expected)) {
				ret &= true;
			}
		} else {
			tmp = make_typeinfo(wk, tc_any, 0);
		}
		break;
	}
	default:
		UNREACHABLE_RETURN;
	}

	if (n->chflg & node_child_d) {
		ret &= analyze_chained(wk, n->d, tmp, res);
	} else {
		*res = tmp;
	}

	return ret;
}

static bool
analyze_func(struct workspace *wk, uint32_t n_id, obj *res)
{
	bool ret = true;

	obj tmp = 0;
	struct node *n = get_node(wk->ast, n_id);

	const char *name = get_node(wk->ast, n->l)->dat.s;

	const struct func_impl_name *fi;

	if (!(fi = func_lookup(func_tbl[obj_default][wk->lang_mode], name))) {
		interp_error(wk, n_id, "function %s not found", name);
		ret = false;

		tmp = make_typeinfo(wk, tc_any, 0);
	} else {
		if (!analyze_function_call(wk, n_id, n->r, fi, 0, &tmp)) {
			ret = false;
		}
	}

	if (n->chflg & node_child_d) {
		return analyze_chained(wk, n->d, tmp, res);
	} else {
		*res = tmp;
		return ret;
	}
}

static bool
analyze_u_minus(struct workspace *wk, struct node *n, obj *res)
{
	obj l_id;

	if (!wk->interp_node(wk, n->l, &l_id)) {
		return false;
	} else if (l_id == disabler_id) {
		*res = disabler_id;
		return true;
	} else if (!typecheck(wk, n->l, l_id, obj_number)) {
		return false;
	}

	if (get_obj_type(wk, l_id) == obj_number) {
		make_obj(wk, res, obj_number);
		set_obj_number(wk, *res, -get_obj_number(wk, l_id));
	} else {
		make_typeinfo(wk, tc_number, 0);
	}
	return true;
}

static void
analyze_arithmetic_cb(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id, enum obj_type lhs, obj *res)
{
	switch (lhs) {
	case obj_string: {
		ctx->expected |= tc_string;
		*res = make_typeinfo(wk, tc_string, 0);
		break;
	}
	case obj_number: {
		ctx->expected |= tc_number;
		*res = make_typeinfo(wk, tc_number, 0);
		break;
	}
	case obj_array: {
		ctx->expected |= tc_any;
		*res = make_typeinfo(wk, tc_array, 0);
		break;
	}
	case obj_dict: {
		ctx->expected |= tc_dict;
		*res = make_typeinfo(wk, tc_dict, 0);
		break;
	}
	default:
		UNREACHABLE;
	}
}


static bool
analyze_arithmetic(struct workspace *wk, uint32_t err_node,
	enum arithmetic_type type, bool plusassign, uint32_t nl, uint32_t nr,
	obj *res)
{
	obj l, r;
	if (!wk->interp_node(wk, nl, &l)) {
		return false;
	}
	if (!wk->interp_node(wk, nr, &r)) {
		return false;
	}

	if (get_obj_type(wk, l) != obj_typeinfo && get_obj_type(wk, r) != obj_typeinfo) {
		return interp_arithmetic(wk, err_node, type, plusassign, nl, nr, res);
	}

	struct analyze_ctx ctx = { 0 };
	uint32_t tc_lhs;

	switch (type) {
	case arith_add:
		tc_lhs = tc_string | tc_number | tc_dict | tc_array;
		break;
	case arith_div:
		tc_lhs = tc_string | tc_number;
		break;
	case arith_sub:
	case arith_mod:
	case arith_mul:
		tc_lhs = tc_number;
		break;
	default:
		assert(false);
		return false;
	}

	if (!typecheck(wk, nl, l, tc_lhs)) {
		return false;
	}

	analyze_for_each_type(wk, &ctx, nl, l, tc_lhs, analyze_arithmetic_cb, res);

	if (!typecheck(wk, nr, r, ctx.expected)) {
		return false;
	}

	return true;
}

static bool
analyze_not(struct workspace *wk, struct node *n, obj *res)
{
	obj obj_l_id;

	if (!wk->interp_node(wk, n->l, &obj_l_id)) {
		return false;
	} else if (obj_l_id == disabler_id) {
		*res = disabler_id;
		return true;
	} else if (!typecheck(wk, n->l, obj_l_id, obj_bool)) {
		return false;
	}

	*res = make_typeinfo(wk, tc_bool, 0);
	return true;
}

static bool
analyze_andor(struct workspace *wk, struct node *n, obj *res)
{
	obj obj_l_id, obj_r_id;

	if (!wk->interp_node(wk, n->l, &obj_l_id)) {
		return false;
	} else if (!typecheck(wk, n->l, obj_l_id, obj_bool)) {
		return false;
	}

	if (!wk->interp_node(wk, n->r, &obj_r_id)) {
		return false;
	} else if (!typecheck(wk, n->r, obj_r_id, obj_bool)) {
		return false;
	}

	*res = make_typeinfo(wk, tc_bool, 0);
	return true;
}

static void
analyze_comparison_cb(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id, enum obj_type lhs, obj *res)
{
	switch (ctx->comparison_type) {
	case comp_equal:
	case comp_nequal:
		ctx->expected |= obj_type_to_tc_type(lhs);
		break;
	case comp_in:
	case comp_not_in:
		ctx->expected |= tc_array | tc_dict;
		break;
	case comp_lt:
	case comp_le:
	case comp_gt:
	case comp_ge:
		ctx->expected |= tc_number;
		break;
	default:
		UNREACHABLE;
	}

	*res = make_typeinfo(wk, tc_bool, 0);
}

static bool
analyze_comparison(struct workspace *wk, struct node *n, obj *res)
{
	obj l, r;

	if (!wk->interp_node(wk, n->l, &l)) {
		return false;
	} else if (!wk->interp_node(wk, n->r, &r)) {
		return false;
	}

	struct analyze_ctx ctx = { .comparison_type = n->subtype };
	uint32_t tc_lhs;

	switch ((enum comparison_type)n->subtype) {
	case comp_equal:
	case comp_nequal:
		tc_lhs = tc_any;
		break;
	case comp_in:
	case comp_not_in:
		tc_lhs = tc_any;
		break;
	case comp_lt:
	case comp_le:
	case comp_gt:
	case comp_ge:
		tc_lhs = tc_number;
		break;
	default:
		UNREACHABLE_RETURN;
	}

	if (!typecheck(wk, n->l, l, tc_lhs)) {
		return false;
	}

	analyze_for_each_type(wk, &ctx, n->l, l, tc_lhs, analyze_comparison_cb, res);

	if (!typecheck(wk, n->r, r, ctx.expected)) {
		return false;
	}

	return true;
}

static bool
analyze_ternary(struct workspace *wk, struct node *n, obj *res)
{
	obj cond_id;
	if (!wk->interp_node(wk, n->l, &cond_id)) {
		return false;
	} else if (cond_id == disabler_id) {
		*res = disabler_id;
		return true;
	} else if (!typecheck(wk, n->l, cond_id, obj_bool)) {
		return false;
	}

	struct obj_typeinfo res_t = { 0 };
	obj a, b;
	wk->interp_node(wk, n->l, &a);
	wk->interp_node(wk, n->r, &b);

	merge_types(wk, &res_t, a);
	merge_types(wk, &res_t, b);

	*res = make_typeinfo(wk, res_t.type, res_t.subtype);
	return true;
}

static bool
analyze_if(struct workspace *wk, struct node *n, obj *res)
{
	bool ret = true;

	switch ((enum if_type)n->subtype) {
	case if_if:
	case if_elseif: {
		obj cond_id;
		if (!wk->interp_node(wk, n->l, &cond_id)) {
			ret = false;
		} else if (cond_id == disabler_id) {
			*res = disabler_id;
			return true;
		} else if (!typecheck(wk, n->l, cond_id, obj_bool)) {
			ret = false;
		}

		break;
	}
	case if_else:
		break;
	default:
		UNREACHABLE_RETURN;
	}

	push_scope_group_scope();
	// if block
	if (!wk->interp_node(wk, n->r, res)) {
		ret = false;
	}

	if (n->chflg & node_child_c) {
		if (!wk->interp_node(wk, n->c, res)) {
			ret = false;
		}
	}

	*res = 0;
	return ret;
}

static bool
analyze_foreach(struct workspace *wk, struct node *n, obj *res)
{
	obj iterable;
	bool ret = false;

	if (!wk->interp_node(wk, n->r, &iterable)) {
		iterable = make_typeinfo(wk, tc_array | tc_dict, 0);
	}

	if (!typecheck(wk, n->r, iterable, tc_array | tc_dict)) {
		ret = false;
		iterable = make_typeinfo(wk, tc_array | tc_dict, 0);
	}

	struct node *args = get_node(wk->ast, n->l);
	uint32_t t = get_obj_type(wk, iterable);
	if (t == obj_typeinfo) {
		t = get_obj_typeinfo(wk, iterable)->type;
	} else {
		t = obj_type_to_tc_type(t);
	}

	bool both = t == (tc_array | tc_dict);
	assert(both || (t == tc_array) || (t == tc_dict));

	if (!both) {
		switch (t) {
		case tc_array:
			if (args->chflg & node_child_r) {
				interp_error(wk, n->l, "array foreach needs exactly one variable to set");
				ret = false;
			}

			break;
		case tc_dict:
			if (!(args->chflg & node_child_r)) {
				interp_error(wk, n->l, "dict foreach needs exactly two variables to set");
				ret = false;
			}

			break;
		default:
			UNREACHABLE;
		}
	}

	push_scope_group();
	push_scope_group_scope();

	uint32_t n_l = args->l, n_r;

	if (args->chflg & node_child_r) {
		// two variables
		n_r = get_node(wk->ast, args->r)->l;
		scope_assign(wk, get_node(wk->ast, n_l)->dat.s, make_typeinfo(wk, tc_string, 0), n_l);
		scope_assign(wk, get_node(wk->ast, n_r)->dat.s, make_typeinfo(wk, tc_any, 0), n_r);
	} else {
		scope_assign(wk, get_node(wk->ast, n_l)->dat.s, make_typeinfo(wk, tc_any, 0), n_l);
	}

	if (!wk->interp_node(wk, n->c, res)) {
		ret = false;
	}

	pop_scope_group(wk);

	return ret;
}

static bool
analyze_stringify(struct workspace *wk, struct node *n, obj *res)
{
	obj l_id;
	*res = make_typeinfo(wk, tc_string, 0);

	if (!wk->interp_node(wk, n->l, &l_id)) {
		return false;
	}

	if (!typecheck(wk, n->l, l_id, tc_bool | tc_file | tc_number | tc_string)) {
		return false;
	}

	return true;
}

static bool
analyze_assign(struct workspace *wk, struct node *n)
{
	bool ret = true;
	obj rhs;
	if (!wk->interp_node(wk, n->r, &rhs)) {
		ret = false;
		rhs = make_typeinfo(wk, tc_any, 0);
	}

	if (!rhs) {
		interp_error(wk, n->l, "assigning variable to null");
	}

	scope_assign(wk, get_node(wk->ast, n->l)->dat.s, rhs, n->l);
	return ret;
}

static bool
analyze_plusassign(struct workspace *wk, uint32_t n_id, obj *_)
{
	struct node *n = get_node(wk->ast, n_id);

	obj rhs;
	if (!analyze_arithmetic(wk, n_id, arith_add, true, n->l, n->r, &rhs)) {
		return false;
	}

	scope_assign(wk, get_node(wk->ast, n->l)->dat.s, rhs, n->l);
	return true;
}

bool
analyze_node(struct workspace *wk, uint32_t n_id, obj *res)
{
	bool ret = true;
	*res = 0;

	struct node *n = get_node(wk->ast, n_id);
	/* L("analyzing node '%s'", node_to_s(n)); */

	++wk->stack_depth;
	if (wk->loop_ctl) {
		--wk->stack_depth;
		return true;
	}

	switch (n->type) {
	/* literals */
	case node_dict:
	case node_array:
	case node_string:
	case node_number:
	case node_bool:
		ret = interp_node(wk, n_id, res);
		break;

	/* control flow */
	case node_block:
		ret = interp_node(wk, n_id, res);
		break;
	case node_if:
		if (n->subtype == if_if) {
			push_scope_group();
		}

		ret = analyze_if(wk, n, res);

		if (n->subtype == if_if) {
			pop_scope_group(wk);
		}
		break;
	case node_foreach:
		ret = analyze_foreach(wk, n, res);
		break;
	case node_continue:
		ret = true;
		break;
	case node_break:
		ret = true;
		break;

	/* functions */
	case node_function:
		ret = analyze_func(wk, n_id, res);
		break;
	case node_method:
	case node_index: {
		obj l_id;
		assert(n->chflg & node_child_l);

		if (!analyze_node(wk, n->l, &l_id)) {
			ret = false;
			break;
		}

		ret = analyze_chained(wk, n_id, l_id, res);
		break;
	}

	/* assignment */
	case node_assignment:
		ret = analyze_assign(wk, n);
		break;
	case node_id: {
		struct assignment *a;
		if (!(a = assign_lookup(wk, n->dat.s))) {
			interp_error(wk, n_id, "undefined object");
			*res = make_typeinfo(wk, tc_any, 0);
			ret = false;
		} else {
			*res = a->o;
			a->accessed = true;
		}
		break;
	}

	/* comparison stuff */
	case node_not:
		ret = analyze_not(wk, n, res);
		break;
	case node_and:
	case node_or:
		ret = analyze_andor(wk, n, res);
		break;
	case node_comparison:
		ret = analyze_comparison(wk, n, res);
		break;
	case node_ternary:
		ret = analyze_ternary(wk, n, res);
		break;

	/* math */
	case node_u_minus:
		ret = analyze_u_minus(wk, n, res);
		break;
	case node_arithmetic:
		ret = analyze_arithmetic(wk, n_id, n->subtype, false, n->l, n->r, res);
		break;
	case node_plusassign:
		ret = analyze_plusassign(wk, n_id, res);
		break;

	/* special */
	case node_stringify:
		ret = analyze_stringify(wk, n, res);
		break;
	case node_empty:
		ret = true;
		break;

	/* never valid */
	case node_foreach_args:
	case node_argument:
	case node_paren:
	case node_empty_line:
	case node_null:
		UNREACHABLE_RETURN;
	}

	--wk->stack_depth;
	if (!ret) {
		analyze_error = true;
	}
	return true;
}

static void
scope_assign_wrapper(struct workspace *wk, const char *name, obj o, uint32_t n_id)
{
	scope_assign(wk, name, o, n_id);
}

bool
do_analyze(void)
{
	bool res = false;;
	struct workspace wk;
	workspace_init(&wk);

	darr_init(&assignment_scopes.groups, 16, sizeof(struct scope_group));
	darr_init(&assignment_scopes.base, 256, sizeof(struct assignment));

	{
		/*
		 * default variables have to be re-assigned with scope_assign
		 * for them to be used in the analyzer
		 */
		const char *default_vars[] = {
			"meson",
			"host_machine",
			"build_machine",
			"target_machine",
		};

		uint32_t i;
		uint64_t obj;
		for (i = 0; i < ARRAY_LEN(default_vars); ++i) {
			obj = *hash_get_str(&wk.scope, default_vars[i]);
			hash_unset_str(&wk.scope, default_vars[i]);
			struct assignment *a = scope_assign(&wk, default_vars[i], obj, 0);
			a->default_var = true;
		}
	}

	wk.interp_node = analyze_node;
	wk.assign_variable = scope_assign_wrapper;

	if (!workspace_setup_dirs(&wk, "dummy", "argv0", false)) {
		goto err;
	}

	uint32_t project_id;
	res = eval_project(&wk, NULL, wk.source_root, wk.build_root, &project_id);

	{
		assert(assignment_scopes.groups.len == 0);
		uint32_t i;
		for (i = 0; i < assignment_scopes.base.len; ++i) {
			struct assignment *a = darr_get(&assignment_scopes.base, i);
			if (!a->default_var && !a->accessed) {
				// TODO: this requires ast information to be preserved after eval_project
				/* interp_error(&wk, a->n_id, "unused variable %s", a->name); */
				LOG_W("unused variable %s", a->name);
				res = false;
			}
		}
	}

	if (analyze_error) {
		res = false;
	}
err:
	darr_destroy(&assignment_scopes.groups);
	darr_destroy(&assignment_scopes.base);
	workspace_destroy(&wk);
	return res;
}
