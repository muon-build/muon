/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

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
static const struct analyze_opts *analyze_opts;

struct analyze_file_entrypoint {
	bool is_root;
	uint32_t line, col, src_idx;
};

static struct darr analyze_entrypoint_stack,
		   analyze_entrypoint_stacks;

struct assignment {
	const char *name;
	obj o;
	bool accessed, default_var;
	uint32_t line, col, src_idx;

	uint32_t ep_stacks_i;
	uint32_t ep_stack_len;
};

struct scope_group {
	struct darr scopes;
};

struct {
	struct darr groups;
	struct darr base;
} assignment_scopes;

static bool
analyze_diagnostic_enabled(enum analyze_diagnostic d)
{
	return analyze_opts->enabled_diagnostics & d;
}

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
make_typeinfo(struct workspace *wk, type_tag t, type_tag sub_t)
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

static type_tag
coerce_type_tag(struct workspace *wk, obj r)
{
	type_tag t = get_obj_type(wk, r);

	if (t == obj_typeinfo) {
		return get_obj_typeinfo(wk, r)->type;
	} else {
		return obj_type_to_tc_type(t);
	}
}

static void
merge_types(struct workspace *wk, struct obj_typeinfo *a, obj r)
{
	a->type |= coerce_type_tag(wk, r);
}

struct analyze_ctx {
	type_tag expected;
	type_tag found;
	const struct func_impl_name *found_func;
	struct obj_typeinfo ti;
	enum comparison_type comparison_type;
	obj l;
};

typedef void ((analyze_for_each_type_cb)(struct workspace *wk,
	struct analyze_ctx *ctx, uint32_t n_id, type_tag t, obj *res));

static void
analyze_for_each_type(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id,
	obj o, type_tag typemask, analyze_for_each_type_cb cb, obj *res)
{
	obj r = 0;

	type_tag t;
	if ((t = get_obj_type(wk, o)) == obj_typeinfo) {
		t = get_obj_typeinfo(wk, o)->type;
	}

	if (t & obj_typechecking_type_tag) {
		if (t == tc_disabler) {
			interp_warning(wk, n_id, "this expression is always disabled");
			*res = make_typeinfo(wk, tc_disabler, 0);
			return;
		} else if ((t & tc_disabler) == tc_disabler) {
			t &= ~tc_disabler;
			t |= obj_typechecking_type_tag;
		}

		struct obj_typeinfo res_t = { 0 };

		if (typemask) {
			t &= typemask;
		}

		type_tag ot;
		for (ot = 1; ot <= tc_type_count; ++ot) {
			type_tag tc = obj_type_to_tc_type(ot);

			if ((t & tc) == tc) {
				r = 0;
				cb(wk, ctx, n_id, ot, &r);
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

static bool
assign_lookup_scope_i(const char *name, struct darr *scope, uint32_t *res)
{
	uint32_t i;
	for (i = 0; i < scope->len; ++i) {
		struct assignment *a = darr_get(scope, i);
		if (strcmp(a->name, name) == 0) {
			*res = i;
			return true;
		}
	}

	return false;
}

static struct assignment *
assign_lookup_scope(const char *name, struct darr *scope)
{
	uint32_t i;
	if (assign_lookup_scope_i(name, scope, &i)) {
		return darr_get(scope, i);
	} else {
		return NULL;
	}
}

static void
analyze_unassign(struct workspace *wk, const char *name)
{
	int32_t i;
	uint32_t idx = 0;
	struct darr *containing_scope = NULL;
	bool is_base = false;

	for (i = assignment_scopes.groups.len - 1; i >= 0; --i) {
		struct scope_group *g = darr_get(&assignment_scopes.groups, i);
		if (!(g->scopes.len)) {
			continue;
		}

		struct darr *scope = darr_get(&g->scopes, g->scopes.len - 1);

		if (assign_lookup_scope_i(name, scope, &idx)) {
			containing_scope = scope;
			break;
		}
	}

	obj _;
	if (!containing_scope
	    && wk->projects.len
	    && get_obj_id(wk, name, &_, wk->cur_project)) {
		if (!assign_lookup_scope_i(name, &assignment_scopes.base, &idx)) {
			UNREACHABLE;
		}

		containing_scope = &assignment_scopes.base;
		is_base = true;
	}

	if (!containing_scope) {
		// variable not found...
		return;
	}

	darr_del(containing_scope, idx);
	if (is_base) {
		hash_unset_str(&current_project(wk)->scope, name);
	}
}

static struct assignment *
assign_lookup(struct workspace *wk, const char *name)
{
	int32_t i;
	uint32_t id;
	struct assignment *found = NULL;

	for (i = assignment_scopes.groups.len - 1; i >= 0; --i) {
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

static void
check_reassign_to_different_type(struct workspace *wk, struct assignment *a, obj new_val, struct assignment *new_a, uint32_t n_id)
{
	if (!analyze_diagnostic_enabled(analyze_diagnostic_reassign_to_conflicting_type)) {
		return;
	}

	type_tag t1 = coerce_type_tag(wk, a->o), t2 = coerce_type_tag(wk, new_val);

	if ((t1 & t2) != t2) {
		char buf[BUF_SIZE_2k] = { 0 };
		snprintf(buf, BUF_SIZE_2k, "reassignment of variable %s with type %s to conflicting type %s",
			a->name,
			typechecking_type_to_s(wk, t1),
			typechecking_type_to_s(wk, t2));

		if (new_a) {
			error_diagnostic_store_push(
				new_a->src_idx,
				new_a->line,
				new_a->col,
				log_warn,
				buf
				);
		} else {
			interp_warning(wk, n_id, "%s", buf);
		}
	}
}

static struct assignment *
scope_assign(struct workspace *wk, const char *name, obj o, uint32_t n_id)
{
	if (!o) {
		interp_error(wk, n_id, "assigning variable to null");
		analyze_error = true;
	}

	struct darr *s;
	if (assignment_scopes.groups.len) {
		struct scope_group *g = darr_get(&assignment_scopes.groups, assignment_scopes.groups.len - 1);

		assert(g->scopes.len);
		s = darr_get(&g->scopes, g->scopes.len - 1);
	} else {
		s = &assignment_scopes.base;
	}

	struct assignment *a;
	if ((a = assign_lookup_scope(name, s))) {
		// re-assign
		check_reassign_to_different_type(wk, a, o, NULL, n_id);
		a->o = o;
		return a;
	}

	// builtin variables don't have a source location
	struct node *n = NULL;
	uint32_t src_idx = 0, ep_stack_len = 0, ep_stacks_i = 0;
	if (wk->src && n_id) {
		n = get_node(wk->ast, n_id);

		// push the source so that we have it later for error reporting
		src_idx = error_diagnostic_store_push_src(wk->src);

		if (analyze_entrypoint_stack.len) {
			ep_stacks_i  = analyze_entrypoint_stacks.len;
			darr_grow_by(&analyze_entrypoint_stacks, analyze_entrypoint_stack.len);
			ep_stack_len = analyze_entrypoint_stack.len;

			memcpy(darr_get(&analyze_entrypoint_stacks, ep_stacks_i),
				analyze_entrypoint_stack.e,
				sizeof(struct analyze_file_entrypoint) * analyze_entrypoint_stack.len);
		}
	}
	darr_push(s, &(struct assignment) {
		.name = name,
		.o = o,
		.line = n ? n->line : 0,
		.col = n ? n->col : 0,
		.src_idx = src_idx,
		.ep_stacks_i = ep_stacks_i,
		.ep_stack_len = ep_stack_len,
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
					// re-assign
					check_reassign_to_different_type(wk, old, a->o, a, 0);
					merge_types(wk, get_obj_typeinfo(wk, old->o), a->o);
				}
			} else {
				darr_push(base, a);
			}
		}
	}

	for (i = 0; i < base->len; ++i) {
		struct assignment *a = darr_get(base, i), *b;

		if ((b = assign_lookup(wk, a->name))) {
			type_tag new_type = get_obj_type(wk, b->o);
			if (new_type != obj_typeinfo) {
				b->o = make_typeinfo(wk, obj_type_to_tc_type(new_type), 0);
			}

			check_reassign_to_different_type(wk, b, a->o, a, 0);
			merge_types(wk, get_obj_typeinfo(wk, b->o), a->o);
		} else {
			b = scope_assign(wk, a->name, a->o, 0);
			b->accessed = a->accessed;
			b->line = a->line;
			b->col = a->col;
			b->src_idx = a->src_idx;
		}
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
analyze_all_function_arguments(struct workspace *wk, uint32_t n_id, uint32_t args_node)
{
	bool ret = true;
	struct node *args = get_node(wk->ast, args_node);
	bool had_kwargs = false;
	obj res;
	uint32_t val_node;

	while (args->type != node_empty) {
		if (args->subtype == arg_kwarg) {
			had_kwargs = true;
			val_node = args->r;
		} else {
			if (had_kwargs) {
				interp_error(wk, args_node, "non-kwarg after kwargs");
				ret = false;
			}
			val_node = args->l;
		}

		if (!wk->interp_node(wk, val_node, &res)) {
			ret = false;
		}

		if (args->chflg & node_child_c) {
			args_node = args->c;
			args = get_node(wk->ast, args_node);
		} else {
			break;
		}
	}

	return ret;
}

static bool
analyze_function_call(struct workspace *wk, uint32_t n_id, uint32_t args_node, const struct func_impl_name *fi, obj rcvr, obj *res)
{
	bool ret = true;
	obj func_res;
	bool old_analyze_error = analyze_error;
	analyze_error = false;

	bool subdir_func = !rcvr && strcmp(fi->name, "subdir") == 0;

	if (subdir_func) {
		struct node *n = get_node(wk->ast, n_id);

		darr_push(&analyze_entrypoint_stack, &(struct analyze_file_entrypoint) {
			.src_idx = error_diagnostic_store_push_src(wk->src),
			.line = n->line,
			.col = n->col,
		});
	}

	bool was_pure;
	if (!analyze_function(wk, fi, args_node, rcvr, &func_res, &was_pure) || analyze_error) {
		if (subdir_func && analyze_opts->subdir_error) {
			interp_error(wk, n_id, "in subdir");
		}
		ret = false;
	}

	if (subdir_func) {
		if (!was_pure) {
			interp_warning(wk, n_id, "unable to analyze subdir call");
		}

		darr_del(&analyze_entrypoint_stack, analyze_entrypoint_stack.len - 1);
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
analyze_method(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id, type_tag rcvr_type, obj *res)
{
	struct node *n = get_node(wk->ast, n_id);

	const char *name = get_node(wk->ast, n->r)->dat.s;
	const struct func_impl_name *fi;

	if (rcvr_type == obj_module
	    && get_obj_type(wk, ctx->l) == obj_module
	    && get_obj_module(wk, ctx->l)->found) {
		struct obj_module *m = get_obj_module(wk, ctx->l);
		enum module mod = m->module;
		if (!(fi = module_func_lookup(wk, name, mod))) {
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
analyze_index(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id, type_tag lhs, obj *res)
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
				analyze_all_function_arguments(wk, n_id, n->c);
				ret = false;
			}
		} else if (ctx.found) {
			if (ctx.expected) {
				tmp = make_typeinfo(wk, ctx.expected, 0);
			}
		} else if (!ctx.found) {
			analyze_all_function_arguments(wk, n_id, n->c);

			type_tag t = get_obj_type(wk, l_id);
			bool rcvr_is_not_found_module = (t == obj_module && !get_obj_module(wk, l_id)->found)
							|| (t == obj_typeinfo && get_obj_typeinfo(wk, l_id)->type == tc_module);
			bool rcvr_is_module_object = t == obj_typeinfo && get_obj_typeinfo(wk, l_id)->subtype == tc_module;

			if (rcvr_is_not_found_module || rcvr_is_module_object) {
				tmp = make_typeinfo(wk, tc_any, tc_module);
			} else {
				interp_error(wk, n_id, "method %s not found on %s", get_node(wk->ast, n->r)->dat.s, inspect_typeinfo(wk, l_id));
				ret = false;
				tmp = make_typeinfo(wk, tc_any, 0);
			}
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
		const type_tag tc_lhs = tc_string | tc_custom_target | tc_dict | tc_array;

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

	if (!(fi = func_lookup(kernel_func_tbl[wk->lang_mode], name))) {
		interp_error(wk, n_id, "function %s not found", name);
		ret = false;

		analyze_all_function_arguments(wk, n_id, n->r);

		tmp = make_typeinfo(wk, tc_any, 0);
	} else {
		if (!analyze_function_call(wk, n_id, n->r, fi, 0, &tmp)) {
			analyze_all_function_arguments(wk, n_id, n->r);
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
	bool ret = true;
	obj l_id;

	if (!wk->interp_node(wk, n->l, &l_id)) {
		ret = false;
	}
	if (ret && !typecheck(wk, n->l, l_id, obj_number)) {
		ret = false;
	}

	if (ret && get_obj_type(wk, l_id) == obj_number) {
		make_obj(wk, res, obj_number);
		set_obj_number(wk, *res, -get_obj_number(wk, l_id));
	} else {
		*res = make_typeinfo(wk, tc_number, 0);
	}
	return ret;
}

static void
analyze_arithmetic_cb(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id, type_tag lhs, obj *res)
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

static enum iteration_result
is_pure_arithmetic_object_dict_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	if (get_obj_type(wk, k) == obj_typeinfo) {
		return ir_err;
	}

	return ir_cont;
}

static bool
is_pure_arithmetic_object(struct workspace *wk, obj o)
{
	switch (get_obj_type(wk, o)) {
	case obj_typeinfo:
		return false;
	case obj_dict:
		return !obj_dict_foreach(wk, o, NULL, is_pure_arithmetic_object_dict_iter);
	default:
		return true;
	}
}

static bool
analyze_arithmetic(struct workspace *wk, uint32_t err_node,
	enum arithmetic_type type, bool plusassign, uint32_t nl, uint32_t nr,
	obj *res)
{
	bool ret = true;
	obj l, r;

	if (!wk->interp_node(wk, nl, &l)) {
		ret = false;
	}
	if (!wk->interp_node(wk, nr, &r)) {
		ret = false;
	}

	if (is_pure_arithmetic_object(wk, l) && is_pure_arithmetic_object(wk, r)) {
		return interp_arithmetic(wk, err_node, type, plusassign, nl, nr, res);
	}

	struct analyze_ctx ctx = { 0 };
	type_tag tc_lhs = tc_disabler;

	switch (type) {
	case arith_add:
		tc_lhs |= tc_string | tc_number | tc_dict | tc_array;
		break;
	case arith_div:
		tc_lhs |= tc_string | tc_number;
		break;
	case arith_sub:
	case arith_mod:
	case arith_mul:
		tc_lhs |= tc_number;
		break;
	default:
		assert(false);
		return false;
	}

	if (ret && !typecheck(wk, nl, l, tc_lhs)) {
		return false;
	}

	if (ret) {
		analyze_for_each_type(wk, &ctx, nl, l, tc_lhs, analyze_arithmetic_cb, res);
	}

	if (ret && !typecheck(wk, nr, r, ctx.expected)) {
		ret = false;
	}

	if (!ret) {
		*res = make_typeinfo(wk, tc_string | tc_number | tc_array | tc_dict, 0);
	}

	return ret;
}

static bool
analyze_not(struct workspace *wk, struct node *n, obj *res)
{
	bool ret = true;
	obj l;

	if (!wk->interp_node(wk, n->l, &l)) {
		ret = false;
	}
	if (ret && !typecheck(wk, n->l, l, obj_bool)) {
		ret = false;
	}

	if (ret && get_obj_type(wk, l) == obj_bool) {
		make_obj(wk, res, obj_bool);
		set_obj_bool(wk, *res, !get_obj_bool(wk, l));
	} else {
		*res = make_typeinfo(wk, tc_bool, 0);
	}
	return ret;
}

static bool
analyze_andor(struct workspace *wk, struct node *n, obj *res)
{
	bool ret = true;
	obj l, r;

	if (!wk->interp_node(wk, n->l, &l)) {
		ret = false;
	} else if (ret && !typecheck(wk, n->l, l, obj_bool)) {
		ret = false;
	}

	if (!wk->interp_node(wk, n->r, &r)) {
		ret = false;
	} else if (ret && !typecheck(wk, n->r, r, obj_bool)) {
		ret = false;
	}

	*res = make_typeinfo(wk, tc_bool, 0);
	return ret;
}

static void
analyze_comparison_cb(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id, type_tag lhs, obj *res)
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
	bool ret = true;
	obj l, r;

	if (!wk->interp_node(wk, n->l, &l)) {
		ret = false;
	}
	if (!wk->interp_node(wk, n->r, &r)) {
		ret = false;
	}

	struct analyze_ctx ctx = { .comparison_type = n->subtype };
	type_tag tc_lhs;

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

	if (ret && !typecheck(wk, n->l, l, tc_lhs)) {
		ret = false;
	}

	if (ret) {
		analyze_for_each_type(wk, &ctx, n->l, l, tc_lhs, analyze_comparison_cb, res);
	}

	if (ret && !typecheck(wk, n->r, r, ctx.expected)) {
		ret = false;
	}

	if (!ret) {
		*res = make_typeinfo(wk, tc_bool, 0);
	}

	return ret;
}

static bool
analyze_ternary(struct workspace *wk, struct node *n, obj *res)
{
	bool ret = true;
	obj cond_id;
	if (!wk->interp_node(wk, n->l, &cond_id)) {
		ret = false;
	} else if (ret && !typecheck(wk, n->l, cond_id, obj_bool)) {
		ret = false;
	}

	struct obj_typeinfo res_t = { 0 };
	obj a = 0, b = 0;

	if (!wk->interp_node(wk, n->r, &a)) {
		ret = false;
	}

	if (!wk->interp_node(wk, n->c, &b)) {
		ret = false;
	}

	if (!a && !b) {
		*res = 0;
		return ret;
	}

	if (a) {
		merge_types(wk, &res_t, a);
	}

	if (b) {
		merge_types(wk, &res_t, b);
	}

	*res = make_typeinfo(wk, res_t.type, res_t.subtype);
	return ret;
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
analyze_foreach(struct workspace *wk, uint32_t n_id, obj *res)
{
	obj iterable;
	bool ret = true;
	struct node *n = get_node(wk->ast, n_id);

	if (!wk->interp_node(wk, n->r, &iterable)) {
		ret = false;
		iterable = make_typeinfo(wk, tc_array | tc_dict, 0);
	}

	if (!typecheck(wk, n->r, iterable, tc_array | tc_dict)) {
		ret = false;
		iterable = make_typeinfo(wk, tc_array | tc_dict, 0);
	}

	struct node *args = get_node(wk->ast, n->l);
	type_tag t = get_obj_type(wk, iterable);
	if (t == obj_typeinfo) {
		t = get_obj_typeinfo(wk, iterable)->type & (tc_array | tc_dict);
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

	if (get_obj_type(wk, iterable) != obj_typeinfo) {
		uint32_t len;
		switch (get_obj_type(wk, iterable)) {
		case obj_dict:
			len = get_obj_dict(wk, iterable)->len;
			break;
		case obj_array:
			len = get_obj_array(wk, iterable)->len;
			break;
		default:
			UNREACHABLE;
		}

		if (len) {
			return interp_node(wk, n_id, res);
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
	/* L("analyzing node '%s'@%d", node_to_s(n)); */

	if (wk->loop_ctl) {
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
		ret = analyze_foreach(wk, n_id, res);
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
			interp_error(wk, n_id, "undefined object %s", n->dat.s);
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

	if (!ret) {
		analyze_error = true;
	}
	return true;
}

static void
analyze_assign_wrapper(struct workspace *wk, const char *name, obj o, uint32_t n_id)
{
	scope_assign(wk, name, o, n_id);
}

static bool
analyze_lookup_wrapper(struct workspace *wk, const char *name, obj *res, uint32_t proj_id)
{
	struct assignment *a = assign_lookup(wk, name);
	if (a) {
		*res = a->o;
		return true;
	} else {
		return false;
	}
}

static bool
analyze_eval_project_file(struct workspace *wk, const char *path, bool first)
{
	const char *newpath = path;
	if (analyze_opts->file_override && strcmp(analyze_opts->file_override, path) == 0) {
		bool ret = false;
		struct source src = { 0 };
		if (!fs_read_entire_file("-", &src)) {
			return false;
		}
		src.label = path;

		obj res;
		if (!eval(wk, &src, first ? eval_mode_first : eval_mode_default, &res)) {
			goto ret;
		}

		ret = true;
ret:
		fs_source_destroy(&src);
		return ret;
	}

	return eval_project_file(wk, newpath, first);
}

static const struct {
	const char *name;
	enum analyze_diagnostic d;
} analyze_diagnostic_names[] = {
	{ "unused-variable", analyze_diagnostic_unused_variable },
	{ "reassign-to-conflicting-type", analyze_diagnostic_reassign_to_conflicting_type },
};

bool
analyze_diagnostic_name_to_enum(const char *name, enum analyze_diagnostic *ret)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(analyze_diagnostic_names); ++i) {
		if (strcmp(analyze_diagnostic_names[i].name, name) == 0) {
			*ret = analyze_diagnostic_names[i].d;
			return true;
		}
	}

	return false;
}

void
analyze_print_diagnostic_names(void)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(analyze_diagnostic_names); ++i) {
		printf("%s\n", analyze_diagnostic_names[i].name);
	}
}

bool
do_analyze(struct analyze_opts *opts)
{
	bool res = false;
	analyze_opts = opts;
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
	wk.assign_variable = analyze_assign_wrapper;
	wk.unassign_variable = analyze_unassign;
	wk.get_variable = analyze_lookup_wrapper;
	wk.eval_project_file = analyze_eval_project_file;
	wk.in_analyzer = true;

	error_diagnostic_store_init();

	darr_init(&analyze_entrypoint_stack, 32, sizeof(struct analyze_file_entrypoint));
	darr_init(&analyze_entrypoint_stacks, 32, sizeof(struct analyze_file_entrypoint));

	uint32_t project_id;
	res = eval_project(&wk, NULL, wk.source_root, wk.build_root, &project_id);

	if (analyze_diagnostic_enabled(analyze_diagnostic_unused_variable)) {
		assert(assignment_scopes.groups.len == 0);
		uint32_t i;
		for (i = 0; i < assignment_scopes.base.len; ++i) {
			struct assignment *a = darr_get(&assignment_scopes.base, i);
			if (!a->default_var && !a->accessed && *a->name != '_') {
				const char *msg = get_cstr(&wk, make_strf(&wk, "unused variable %s", a->name));
				enum log_level lvl = log_warn;
				if (analyze_opts->unused_variable_error) {
					lvl = log_error;
					res = false;
				}
				error_diagnostic_store_push(a->src_idx, a->line, a->col, lvl, msg);

				if (analyze_opts->subdir_error && a->ep_stack_len) {
					uint32_t j;
					struct analyze_file_entrypoint *ep_stack
						= darr_get(&analyze_entrypoint_stacks, a->ep_stacks_i);

					for (j = 0; j < a->ep_stack_len; ++j) {
						error_diagnostic_store_push(
							ep_stack[j].src_idx,
							ep_stack[j].line,
							ep_stack[j].col,
							lvl,
							"in subdir"
							);
					}
				}
			}
		}
	}

	error_diagnostic_store_replay(analyze_opts->replay_opts);

	if (analyze_error) {
		res = false;
	}

	darr_destroy(&analyze_entrypoint_stack);
	darr_destroy(&analyze_entrypoint_stacks);
	darr_destroy(&assignment_scopes.groups);
	darr_destroy(&assignment_scopes.base);
	workspace_destroy(&wk);
	return res;
}
