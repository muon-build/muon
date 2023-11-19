/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "datastructures/hash.h"
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
	bool is_root, has_diagnostic;
	enum log_level lvl;
	uint32_t line, col, src_idx;
};

static struct arr analyze_entrypoint_stack,
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
	struct arr scopes;
};

struct {
	struct arr groups;
	struct arr local;
	struct arr global;
} assignment_scopes;

static void
copy_analyze_entrypoint_stack(uint32_t *ep_stacks_i, uint32_t *ep_stack_len)
{
	if (analyze_entrypoint_stack.len) {
		*ep_stacks_i = analyze_entrypoint_stacks.len;
		arr_grow_by(&analyze_entrypoint_stacks, analyze_entrypoint_stack.len);
		*ep_stack_len = analyze_entrypoint_stack.len;

		memcpy(arr_get(&analyze_entrypoint_stacks, *ep_stacks_i),
			analyze_entrypoint_stack.e,
			sizeof(struct analyze_file_entrypoint) * analyze_entrypoint_stack.len);
	} else {
		*ep_stacks_i = 0;
		*ep_stack_len = 0;
	}
}

static void
mark_analyze_entrypoint_as_containing_diagnostic(uint32_t ep_stacks_i, enum log_level lvl)
{
	struct analyze_file_entrypoint *ep
		= arr_get(&analyze_entrypoint_stacks, ep_stacks_i);

	ep->has_diagnostic = true;
	ep->lvl = lvl;
}

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
	obj found_func_obj, found_func_module;
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
			ctx->expected = tc_any;
			*res = make_typeinfo(wk, tc_disabler, 0);
			ctx->found = 2; // set found to > 1 to indicate the
			                // method exists but it is unknown
			                // which one it is.
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
assign_lookup_scope_i(const char *name, struct arr *scope, uint32_t *res)
{
	uint32_t i;
	for (i = 0; i < scope->len; ++i) {
		struct assignment *a = arr_get(scope, i);
		if (strcmp(a->name, name) == 0) {
			*res = i;
			return true;
		}
	}

	return false;
}

static struct assignment *
assign_lookup_scope(const char *name, struct arr *scope)
{
	uint32_t i;
	if (assign_lookup_scope_i(name, scope, &i)) {
		return arr_get(scope, i);
	} else {
		return NULL;
	}
}

static void
analyze_unassign(struct workspace *wk, const char *name)
{
	int32_t i;
	uint32_t idx = 0;
	struct arr *containing_scope = NULL;

	for (i = assignment_scopes.groups.len - 1; i >= 0; --i) {
		struct scope_group *g = arr_get(&assignment_scopes.groups, i);
		if (!(g->scopes.len)) {
			continue;
		}

		struct arr *scope = arr_get(&g->scopes, g->scopes.len - 1);

		if (assign_lookup_scope_i(name, scope, &idx)) {
			containing_scope = scope;
			break;
		}
	}

	if (!containing_scope && assign_lookup_scope_i(name, &assignment_scopes.global, &idx)) {
		containing_scope = &assignment_scopes.global;
	}

	if (!containing_scope) {
		// variable not found...
		return;
	}

	arr_del(containing_scope, idx);
}

static struct assignment *
assign_lookup(struct workspace *wk, const char *name)
{
	int32_t i;
	struct assignment *found = NULL;

	for (i = assignment_scopes.groups.len - 1; i >= 0; --i) {
		struct scope_group *g = arr_get(&assignment_scopes.groups, i);
		if (!(g->scopes.len)) {
			continue;
		}

		struct arr *scope = arr_get(&g->scopes, g->scopes.len - 1);
		if ((found = assign_lookup_scope(name, scope))) {
			break;
		}
	}

	if (!found && assignment_scopes.local.len) {
		struct arr *s = arr_get(&assignment_scopes.local, assignment_scopes.local.len - 1);
		found = assign_lookup_scope(name, s);
	}

	if (!found) {
		found = assign_lookup_scope(name, &assignment_scopes.global);
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
scope_assign(struct workspace *wk, const char *name, obj o, uint32_t n_id, bool local)
{
	/* if we assigned to null, throw an error but continue with a tc_any */
	if (!o) {
		interp_error(wk, n_id, "assigning variable to null");
		analyze_error = true;
		o = make_typeinfo(wk, tc_any, 0);
	}

	struct arr *s;
	if (assignment_scopes.groups.len) {
		/* If we are in a scope group, retrieve the current scope as the last element of the last scope group  */
		struct scope_group *g = arr_get(&assignment_scopes.groups, assignment_scopes.groups.len - 1);

		assert(g->scopes.len);
		s = arr_get(&g->scopes, g->scopes.len - 1);
	} else {
		if (!local && assignment_scopes.local.len) {
			s = arr_get(&assignment_scopes.local, assignment_scopes.local.len - 1);
			local = assign_lookup_scope(name, s);
		}

		if (local) {
			s = arr_get(&assignment_scopes.local, assignment_scopes.local.len - 1);
		} else {
			s = &assignment_scopes.global;
		}
	}

	struct assignment *a;
	if (wk->impure_loop_depth && (a = assign_lookup(wk, name))) {
		// When overwriting a variable in a loop turn it into a
		// typeinfo so that it gets marked as impure.
		enum obj_type new_type = get_obj_type(wk, o);
		if (new_type != obj_typeinfo && !obj_equal(wk, a->o, o)) {
			o = make_typeinfo(wk, obj_type_to_tc_type(new_type), 0);
		}
	}

	if ((a = assign_lookup_scope(name, s))) {
		// The assignment was found so just reassign it here
		check_reassign_to_different_type(wk, a, o, NULL, n_id);

		a->o = o;
		return a;
	}

	// initialize source location to 0 since some variables don't have
	// anything to put there, like builtin variables
	struct node *n = NULL;
	uint32_t src_idx = 0, ep_stack_len = 0, ep_stacks_i = 0;
	if (wk->src && n_id) {
		n = get_node(wk->ast, n_id);

		// push the source so that we have it later for error reporting
		src_idx = error_diagnostic_store_push_src(wk->src);
		copy_analyze_entrypoint_stack(&ep_stacks_i, &ep_stack_len);
	}

	// Add the new assignment to the current scope and return a pointer to
	// it
	arr_push(s, &(struct assignment) {
		.name = name,
		.o = o,
		.line = n ? n->line : 0,
		.col = n ? n->col : 0,
		.src_idx = src_idx,
		.ep_stacks_i = ep_stacks_i,
		.ep_stack_len = ep_stack_len,
	});

	return arr_get(s, s->len - 1);
}

static void
push_scope_group_scope(void)
{
	assert(assignment_scopes.groups.len);
	struct scope_group *g = arr_get(&assignment_scopes.groups, assignment_scopes.groups.len - 1);
	arr_push(&g->scopes, &(struct arr) { 0 });
	struct arr *scope = arr_get(&g->scopes, g->scopes.len - 1);
	arr_init(scope, 256, sizeof(struct assignment));
}

static void
push_scope_group(void)
{
	struct scope_group g = { 0 };
	arr_init(&g.scopes, 4, sizeof(struct arr));
	arr_push(&assignment_scopes.groups, &g);
}

static void
merge_objects(struct workspace *wk, struct assignment *new, struct assignment *old)
{
	type_tag new_type = get_obj_type(wk, new->o);
	type_tag old_type = get_obj_type(wk, old->o);

	if (new_type != obj_typeinfo) {
		new->o = make_typeinfo(wk, obj_type_to_tc_type(new_type), 0);
	}

	if (old_type != obj_typeinfo) {
		old->o = make_typeinfo(wk, obj_type_to_tc_type(old_type), 0);
	}

	check_reassign_to_different_type(wk, new, old->o, old, 0);
	merge_types(wk, get_obj_typeinfo(wk, new->o), old->o);

	assert(get_obj_type(wk, new->o) == obj_typeinfo);
	assert(get_obj_type(wk, old->o) == obj_typeinfo);
}

static void
pop_scope_group(struct workspace *wk)
{
	assert(assignment_scopes.groups.len);
	size_t idx = assignment_scopes.groups.len - 1;
	struct scope_group *g = arr_get(&assignment_scopes.groups, idx);
	arr_del(&assignment_scopes.groups, idx);

	if (!g->scopes.len) {
		// empty scope group
		return;
	}

	struct arr *global = arr_get(&g->scopes, 0);

	uint32_t i, j;
	for (i = 1; i < g->scopes.len; ++i) {
		struct arr *scope = arr_get(&g->scopes, i);
		for (j = 0; j < scope->len; ++j) {
			struct assignment *b, *a = arr_get(scope, j);
			if ((b = assign_lookup_scope(a->name, global))) {
				merge_objects(wk, b, a);
			} else {
				arr_push(global, a);
			}
		}
	}

	for (i = 0; i < global->len; ++i) {
		struct assignment *a = arr_get(global, i), *b;
		if ((b = assign_lookup(wk, a->name))) {
			merge_objects(wk, b, a);
		} else {
			type_tag type = get_obj_type(wk, a->o);
			if (type != obj_typeinfo) {
				a->o = make_typeinfo(wk, obj_type_to_tc_type(type), 0);
			}

			b = scope_assign(wk, a->name, a->o, 0, false);
			b->accessed = a->accessed;
			b->line = a->line;
			b->col = a->col;
			b->src_idx = a->src_idx;
		}
	}

	for (i = 0; i < g->scopes.len; ++i) {
		struct arr *scope = arr_get(&g->scopes, i);
		arr_destroy(scope);
	}
	arr_destroy(&g->scopes);
}

/*
 *-----------------------------------------------------------------------------
 */

static enum iteration_result
is_dict_with_pure_keys_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	if (get_obj_type(wk, k) == obj_typeinfo) {
		return ir_err;
	}

	return ir_cont;
}

static bool
is_dict_with_pure_keys(struct workspace *wk, obj o)
{
	return obj_dict_foreach(wk, o, NULL, is_dict_with_pure_keys_iter);
}

static bool
is_pure_arithmetic_object(struct workspace *wk, obj o)
{
	switch (get_obj_type(wk, o)) {
	case obj_typeinfo:
	case obj_disabler:
		return false;
	case obj_dict:
		return is_dict_with_pure_keys(wk, o);
	default:
		return true;
	}
}

static void
mark_node_visited(struct node *n)
{
	n->chflg |= node_visited;
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
		mark_node_visited(args);

		if (args->subtype == arg_kwarg) {
			had_kwargs = true;
			val_node = args->r;

			mark_node_visited(get_node(wk->ast, args->l));
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
analyze_func_obj_call(struct workspace *wk, uint32_t n_id, uint32_t args_node, obj func_obj, obj func_module, obj *res)
{
	bool ret = true;
	bool old_analyze_error = analyze_error;
	analyze_error = false;
	if (!func_obj_eval(wk, func_obj, func_module, args_node, res) || analyze_error) {
		interp_error(wk, n_id, "in function");
		ret = false;
	}
	analyze_error = old_analyze_error;
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

	analyze_all_function_arguments(wk, n_id, args_node);

	if (subdir_func) {
		struct node *n = get_node(wk->ast, n_id);

		arr_push(&analyze_entrypoint_stack, &(struct analyze_file_entrypoint) {
			.src_idx = error_diagnostic_store_push_src(wk->src),
			.line = n->line,
			.col = n->col,
			.is_root = analyze_entrypoint_stack.len == 0,
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

		arr_del(&analyze_entrypoint_stack, analyze_entrypoint_stack.len - 1);
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
	mark_node_visited(get_node(wk->ast, n->r));

	obj func_obj = 0, func_module = 0;
	const struct func_impl_name *fi = 0;

	if (rcvr_type == obj_module
	    && get_obj_type(wk, ctx->l) == obj_module
	    && get_obj_module(wk, ctx->l)->found) {
		struct obj_module *m = get_obj_module(wk, ctx->l);
		if (m->exports) {
			if (!obj_dict_index_str(wk, m->exports, name, &func_obj)) {
				return;
			}

			func_module = ctx->l;
		} else {
			enum module mod = m->module;
			if (!(fi = module_func_lookup(wk, name, mod))) {
				return;
			}
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

	if (fi && fi->return_type) {
		*res = make_typeinfo(wk, fi->return_type, 0);
	}

	++ctx->found;
	ctx->found_func_obj = func_obj;
	ctx->found_func_module = func_module;
	ctx->found_func = fi;

	return;
}

static void
analyze_index(struct workspace *wk, struct analyze_ctx *ctx, uint32_t n_id, type_tag lhs, obj *res)
{
	switch (lhs) {
	case obj_disabler:
		ctx->expected |= tc_any;
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
analyze_func(struct workspace *wk, uint32_t n_id, bool chained, obj l_id, obj *res)
{
	bool ret = true;

	obj tmp = 0;
	struct node *n = get_node(wk->ast, n_id);
	const char *name = 0;
	if (!chained) {
		struct node *l = get_node(wk->ast, n->l);
		if (l->type == node_id && func_lookup(kernel_func_tbl[wk->lang_mode], l->dat.s)) {
			name = l->dat.s;
			mark_node_visited(get_node(wk->ast, n->l));
		} else {
			if (!wk->interp_node(wk, n->l, &l_id)) {
				l_id = 0;
				ret = false;
			}
		}
	}

	const struct func_impl_name *fi = 0;
	if (name) {
		fi = func_lookup(kernel_func_tbl[wk->lang_mode], name);
	} else {
		if (!typecheck(wk, n->l, l_id, obj_func)) {
			l_id = 0;
		}
	}

	if (!fi && !l_id) {
		if (name) {
			interp_error(wk, n_id, "function %s not found", name);
		}
		ret = false;

		analyze_all_function_arguments(wk, n_id, n->r);

		tmp = make_typeinfo(wk, tc_any, 0);
	} else if (fi) {
		if (!analyze_function_call(wk, n_id, n->r, fi, 0, &tmp)) {
			ret = false;
		}
	} else if (l_id && get_obj_type(wk, l_id) != obj_typeinfo) {
		if (!analyze_func_obj_call(wk, n_id, n->r, l_id, 0, &tmp)) {
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
analyze_chained(struct workspace *wk, uint32_t n_id, obj l_id, obj *res)
{
	bool ret = true;
	struct node *n = get_node(wk->ast, n_id);
	mark_node_visited(n);
	obj tmp = 0;

	switch (n->type) {
	case node_method: {
		struct analyze_ctx ctx = { .l = l_id };

		analyze_for_each_type(wk, &ctx, n_id, l_id, 0, analyze_method, &tmp);

		if (ctx.found == 1) {
			if (ctx.found_func) {
				if (!analyze_function_call(wk, n_id, n->c, ctx.found_func, l_id, &tmp)) {
					ret = false;
				}
			} else {
				if (!analyze_func_obj_call(wk, n_id, n->c, ctx.found_func_obj, ctx.found_func_module, &tmp)) {
					ret = false;
				}
			}
		} else if (ctx.found) {
			analyze_all_function_arguments(wk, n_id, n->c);

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

			if (is_pure_arithmetic_object(wk, l_id) && is_pure_arithmetic_object(wk, r)) {
				ret &= interp_index(wk, n, l_id, false, &tmp);
			}
		} else {
			tmp = make_typeinfo(wk, tc_any, 0);
		}
		break;
	}
	case node_function: {
		if (!(ret = analyze_func(wk, n_id, true, l_id, &tmp))) {
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

	if (is_pure_arithmetic_object(wk, l)) {
		bool cond = get_obj_bool(wk, l);

		if (n->type == node_and && !cond) {
			make_obj(wk, res, obj_bool);
			set_obj_bool(wk, *res, false);
			return true;
		} else if (n->type == node_or && cond) {
			make_obj(wk, res, obj_bool);
			set_obj_bool(wk, *res, true);
			return true;
		}
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
		ctx->expected |= tc_array | tc_dict | tc_string;
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

	if (is_pure_arithmetic_object(wk, l) && is_pure_arithmetic_object(wk, r)) {
		ret = interp_comparison(wk, n, res);
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

	bool pure_cond = false, cond = false;
	if (ret && is_pure_arithmetic_object(wk, cond_id)) {
		pure_cond = true;
		cond = get_obj_bool(wk, cond_id);
	}

	struct obj_typeinfo res_t = { 0 };
	obj a = 0, b = 0;

	if (!pure_cond || (pure_cond && cond)) {
		if (!wk->interp_node(wk, n->r, &a)) {
			ret = false;
		}

		if (ret && pure_cond) {
			*res = a;
			return true;
		}
	}

	if (!pure_cond || (pure_cond && !cond)) {
		if (!wk->interp_node(wk, n->c, &b)) {
			ret = false;
		}

		if (ret && pure_cond) {
			*res = b;
			return true;
		}
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
	// Push the scope group before evaluating the condition, because the
	// condition may involve variables that were set in the block of the
	// previous branch, e.g.
	//
	// if a == 1 a = 2 elseif a == 3 a = 3 endif
	//
	// If push_scope_group_scope() happened _after_ evaulating the
	// condition, then inside the expression `elseif a == 2`, `a` would be
	// looked up in the previous branches scope, leading the analyzer to
	// wrongly conclude the value `a` is constant 2 in the current scope,
	// and marking the elsif branch as dead code.
	push_scope_group_scope();

	bool ret = true;
	bool pure_cond = false, cond = false;

	switch ((enum if_type)n->subtype) {
	case if_if:
	case if_elseif: {
		obj cond_id;
		if (!wk->interp_node(wk, n->l, &cond_id)) {
			ret = false;
		} else if (!typecheck(wk, n->l, cond_id, obj_bool)) {
			ret = false;
		}

		if (ret && is_pure_arithmetic_object(wk, cond_id)) {
			cond = get_obj_bool(wk, cond_id);
			pure_cond = true;
		}
		break;
	}
	case if_else:
		cond = true;
		pure_cond = true;
		break;
	default:
		UNREACHABLE_RETURN;
	}

	if (pure_cond && !cond) {
		// don't evaluate this block
	} else {
		// if block
		if (!wk->interp_node(wk, n->r, res)) {
			ret = false;
		}
	}

	if (pure_cond && cond) {
		return true;
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


	uint32_t n_l = args->l, n_r;
	if (args->chflg & node_child_r) {
		// two variables
		n_r = get_node(wk->ast, args->r)->l;
		mark_node_visited(get_node(wk->ast, n_l));
		mark_node_visited(get_node(wk->ast, n_r));
	} else {
		mark_node_visited(get_node(wk->ast, n_l));
	}

	if (get_obj_type(wk, iterable) != obj_typeinfo) {
		/* uint32_t len; */
		/* switch (get_obj_type(wk, iterable)) { */
		/* case obj_dict: */
		/* 	len = get_obj_dict(wk, iterable)->len; */
		/* 	break; */
		/* case obj_array: */
		/* 	len = get_obj_array(wk, iterable)->len; */
		/* 	break; */
		/* default: */
		/* 	UNREACHABLE; */
		/* } */

		/* if (len) { */
		return interp_node(wk, n_id, res);
		/* } */
	}

	push_scope_group();
	push_scope_group_scope();

	if (args->chflg & node_child_r) {
		// two variables
		n_r = get_node(wk->ast, args->r)->l;

		scope_assign(wk, get_node(wk->ast, n_l)->dat.s, make_typeinfo(wk, tc_string, 0), n_l, false);
		scope_assign(wk, get_node(wk->ast, n_r)->dat.s, make_typeinfo(wk, tc_any, 0), n_r, false);
	} else {
		scope_assign(wk, get_node(wk->ast, n_l)->dat.s, make_typeinfo(wk, tc_any, 0), n_l, false);
	}

	++wk->impure_loop_depth;
	if (!wk->interp_node(wk, n->c, res)) {
		ret = false;
	}

	// interpret a second time to catch variables whose constness are
	// invalidated by the first iteration, e.g.
	// i += 1
	if (!wk->interp_node(wk, n->c, res)) {
		ret = false;
	}
	--wk->impure_loop_depth;

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

	if (is_pure_arithmetic_object(wk, l_id)) {
		return interp_stringify(wk, n, res);
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

	mark_node_visited(get_node(wk->ast, n->l));

	scope_assign(wk, get_node(wk->ast, n->l)->dat.s, rhs, n->l, n->subtype);
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

	scope_assign(wk, get_node(wk->ast, n->l)->dat.s, rhs, n->l, false);
	return true;
}

bool
analyze_node(struct workspace *wk, uint32_t n_id, obj *res)
{
	bool ret = true;
	*res = 0;

	struct node *n = get_node(wk->ast, n_id);
	mark_node_visited(n);
	/* L("analyzing node '%s'@%d", node_to_s(n), n_id); */

	if (wk->loop_ctl || wk->returning) {
		return true;
	}

	switch (n->type) {
	/* literals */
	case node_dict:
	case node_array:
	case node_string:
	case node_number:
	case node_bool:
	case node_return:
		ret = interp_node(wk, n_id, res);
		break;
	case node_func_def:
		error_diagnostic_store_push_src(wk->src);
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
		ret = analyze_func(wk, n_id, false, 0, res);
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

void
analyze_check_dead_code(struct workspace *wk, struct ast *ast)
{
	if (!analyze_diagnostic_enabled(analyze_diagnostic_dead_code)) {
		return;
	}

	uint32_t i;
	for (i = 0; i < ast->nodes.len; ++i) {
		struct node *n = arr_get(&ast->nodes, i);
		switch (n->type) {
		case node_foreach_args:
		case node_paren:
		case node_empty_line:
		case node_null:
		case node_empty:
		case node_block:
		case node_argument:
		case node_id:
		case node_string:
		case node_number:
			continue;
		/* continue; */
		default:
			break;
		}

		if (!(n->chflg & node_visited)) {
			error_diagnostic_store_push(ast->src_id, n->line, n->col, log_warn, "dead code");

			if (analyze_entrypoint_stack.len) {
				uint32_t ep_stacks_i, ep_stack_len;
				copy_analyze_entrypoint_stack(&ep_stacks_i, &ep_stack_len);
				mark_analyze_entrypoint_as_containing_diagnostic(ep_stacks_i, log_warn);
			}
		}
	}
}

static void
analyze_assign_wrapper(struct workspace *wk, const char *name, obj o, uint32_t n_id, bool local)
{
	scope_assign(wk, name, o, n_id, local);
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

static void
analyze_push_local_scope(struct workspace *wk)
{
	struct arr *s;
	arr_grow_by(&assignment_scopes.local, 1);
	s = arr_get(&assignment_scopes.local, assignment_scopes.local.len - 1);
	arr_init(s, 256, sizeof(struct assignment));
}

static void
analyze_pop_local_scope(struct workspace *wk)
{
	struct arr *s;
	s = arr_get(&assignment_scopes.local, assignment_scopes.local.len - 1);
	arr_destroy(s);
	arr_del(&assignment_scopes.local, assignment_scopes.local.len - 1);
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
	{ "dead-code", analyze_diagnostic_dead_code },
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

	arr_init(&assignment_scopes.groups, 16, sizeof(struct scope_group));
	arr_init(&assignment_scopes.local, 16, sizeof(struct arr));
	arr_init(&assignment_scopes.global, 256, sizeof(struct assignment));

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
			struct assignment *a = scope_assign(&wk, default_vars[i], obj, 0, false);
			a->default_var = true;
		}
	}

	wk.interp_node = analyze_node;
	wk.assign_variable = analyze_assign_wrapper;
	wk.unassign_variable = analyze_unassign;
	wk.push_local_scope = analyze_push_local_scope;
	wk.pop_local_scope = analyze_pop_local_scope;
	wk.get_variable = analyze_lookup_wrapper;
	wk.eval_project_file = analyze_eval_project_file;
	wk.in_analyzer = true;

	error_diagnostic_store_init();

	arr_init(&analyze_entrypoint_stack, 32, sizeof(struct analyze_file_entrypoint));
	arr_init(&analyze_entrypoint_stacks, 32, sizeof(struct analyze_file_entrypoint));

	if (analyze_opts->file_override) {
		const char *root = determine_project_root(&wk, analyze_opts->file_override);
		if (root) {
			SBUF(cwd);
			path_cwd(&wk, &cwd);

			if (strcmp(cwd.buf, root) != 0) {
				path_chdir(root);
				wk.source_root = root;
			}
		}
	}

	if (opts->internal_file) {
		uint32_t proj_id;
		make_project(&wk, &proj_id, "dummy", wk.source_root, wk.build_root);

		struct assignment *a = scope_assign(&wk, "argv", make_typeinfo(&wk, tc_array, 0), 0, false);
		a->default_var = true;

		wk.lang_mode = language_internal;

		struct source src;
		if (!fs_read_entire_file(opts->internal_file, &src)) {
			res = false;
		} else {
			obj _v;
			res = eval(&wk, &src, eval_mode_default, &_v);
		}

		fs_source_destroy(&src);
	} else {
		uint32_t project_id;
		res = eval_project(&wk, NULL, wk.source_root, wk.build_root, &project_id);
	}

	if (analyze_diagnostic_enabled(analyze_diagnostic_unused_variable)) {
		assert(assignment_scopes.groups.len == 0);
		uint32_t i;
		for (i = 0; i < assignment_scopes.global.len; ++i) {
			struct assignment *a = arr_get(&assignment_scopes.global, i);
			if (!a->default_var && !a->accessed && *a->name != '_') {
				const char *msg = get_cstr(&wk, make_strf(&wk, "unused variable %s", a->name));
				enum log_level lvl = log_warn;
				error_diagnostic_store_push(a->src_idx, a->line, a->col, lvl, msg);

				if (analyze_opts->subdir_error && a->ep_stack_len) {
					mark_analyze_entrypoint_as_containing_diagnostic(a->ep_stacks_i, lvl);
				}
			}
		}
	}

	{
		uint32_t i;
		for (i = 0; i < wk.asts.len; ++i) {
			wk.ast = bucket_arr_get(&wk.asts, i);
			analyze_check_dead_code(&wk, wk.ast);
		}
	}

	uint32_t i;
	for (i = 0; i < analyze_entrypoint_stacks.len;) {
		uint32_t j;
		struct analyze_file_entrypoint *ep_stack = arr_get(&analyze_entrypoint_stacks, i);
		assert(ep_stack->is_root);

		uint32_t len = 1;
		for (j = 1; j + i < analyze_entrypoint_stacks.len && !ep_stack[j].is_root; ++j) {
			++len;
		}

		if (ep_stack->has_diagnostic) {
			enum log_level lvl = ep_stack->lvl;

			for (j = 0; j < len; ++j) {
				error_diagnostic_store_push(
					ep_stack[j].src_idx,
					ep_stack[j].line,
					ep_stack[j].col,
					lvl,
					"in subdir"
					);
			}
		}

		i += len;
	}

	bool saw_error;
	error_diagnostic_store_replay(analyze_opts->replay_opts, &saw_error);

	if (saw_error || analyze_error) {
		res = false;
	}

	arr_destroy(&analyze_entrypoint_stack);
	arr_destroy(&analyze_entrypoint_stacks);
	arr_destroy(&assignment_scopes.groups);
	arr_destroy(&assignment_scopes.local);
	arr_destroy(&assignment_scopes.global);
	workspace_destroy(&wk);
	return res;
}
