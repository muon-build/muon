/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "lang/analyze.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "tracy.h"

static struct {
	bool error;
	uint32_t impure_loop_depth, impure_branch_depth;
	const struct analyze_opts *opts;
	obj eval_trace;
	struct obj_func *fp;
	struct vm_ops unpatched_ops;
} analyzer;

struct analyze_file_entrypoint {
	bool is_root, has_diagnostic;
	enum log_level lvl;
	struct source_location location;
	uint32_t src_idx;
};

static struct arr analyze_entrypoint_stack, analyze_entrypoint_stacks;

struct assignment {
	const char *name;
	obj o;
	bool accessed, default_var;
	struct source_location location;
	uint32_t src_idx;

	uint32_t ep_stacks_i;
	uint32_t ep_stack_len;
};

struct bucket_arr assignments;

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
	struct analyze_file_entrypoint *ep = arr_get(&analyze_entrypoint_stacks, ep_stacks_i);

	ep->has_diagnostic = true;
	ep->lvl = lvl;
}

static bool
analyze_diagnostic_enabled(enum analyze_diagnostic d)
{
	return analyzer.opts->enabled_diagnostics & d;
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

type_tag
flatten_type(struct workspace *wk, type_tag t)
{
	if (!(t & TYPE_TAG_COMPLEX)) {
		return t;
	}

	uint32_t idx = COMPLEX_TYPE_INDEX(t);
	enum complex_type ct = COMPLEX_TYPE_TYPE(t);

	struct bucket_arr *typeinfo_arr = &wk->vm.objects.obj_aos[obj_typeinfo - _obj_aos_start];
	struct obj_typeinfo *ti = bucket_arr_get(typeinfo_arr, idx);

	switch (ct) {
	case complex_type_or: return flatten_type(wk, ti->type) | flatten_type(wk, ti->subtype);
	case complex_type_nested: return ti->type;
	}

	UNREACHABLE_RETURN;
}

static obj
make_typeinfo(struct workspace *wk, type_tag t)
{
	obj res;
	make_obj(wk, &res, obj_typeinfo);
	struct obj_typeinfo *type = get_obj_typeinfo(wk, res);
	type->type = t;
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

struct check_analyze_scope_ctx {
	const char *name;
	uint32_t i;
	obj res, scope;
	bool found;
};

// example local_scope: [{a: 1}, [{b: 2}, {b: 3}], [{c: 4}]]
static enum iteration_result
analyze_check_scope_stack(struct workspace *wk, void *_ctx, obj local_scope)
{
	TracyCZoneAutoS;
	struct check_analyze_scope_ctx *ctx = _ctx;

	obj base;
	obj_array_index(wk, local_scope, 0, &base);

	uint32_t local_scope_len = get_obj_array(wk, local_scope)->len;
	if (local_scope_len > 1) {
		int32_t i;
		// example: {a: 1},           -- skip
		// example: [{b: 2}, {b: 3}], -- take last
		// example: [{c: 4}]          -- take last
		for (i = local_scope_len - 1; i >= 1; --i) {
			struct check_analyze_scope_ctx *ctx = _ctx;

			obj scope_group;
			obj_array_index(wk, local_scope, i, &scope_group);
			obj scope = obj_array_get_tail(wk, scope_group);

			if (obj_dict_index_str(wk, scope, ctx->name, &ctx->res)) {
				ctx->scope = scope;
				ctx->found = true;
				break;
			}
		}
	}

	if (!ctx->found) {
		if (obj_dict_index_str(wk, base, ctx->name, &ctx->res)) {
			ctx->scope = base;
			ctx->found = true;
		}
	}

	TracyCZoneAutoE;
	return ir_cont;
}

// example scope_stack: [[{a: 1}, [{b: 2}, {b: 3}], [{c: 4}]]]
static struct assignment *
assign_lookup(struct workspace *wk, const char *name)
{
	TracyCZoneAutoS;
	struct check_analyze_scope_ctx ctx = { .name = name };
	obj_array_foreach(wk, wk->vm.scope_stack, &ctx, analyze_check_scope_stack);

	if (ctx.found) {
		TracyCZoneAutoE;
		return bucket_arr_get(&assignments, ctx.res);
	} else {
		TracyCZoneAutoE;
		return 0;
	}
}

static obj
assign_lookup_scope(struct workspace *wk, const char *name)
{
	TracyCZoneAutoS;
	struct check_analyze_scope_ctx ctx = { .name = name };
	obj_array_foreach(wk, wk->vm.scope_stack, &ctx, analyze_check_scope_stack);

	if (ctx.found) {
		TracyCZoneAutoE;
		return ctx.scope;
	} else {
		TracyCZoneAutoE;
		return 0;
	}
}

static void
analyze_unassign(struct workspace *wk, const char *name)
{
	obj scope = assign_lookup_scope(wk, name);
	if (scope) {
		obj_dict_del_strn(wk, scope, name, strlen(name));
	}
}

static void
check_reassign_to_different_type(struct workspace *wk,
	struct assignment *a,
	obj new_val,
	struct assignment *new_a,
	uint32_t n_id)
{
	if (!analyze_diagnostic_enabled(analyze_diagnostic_reassign_to_conflicting_type)) {
		return;
	}

	type_tag t1 = coerce_type_tag(wk, a->o), t2 = coerce_type_tag(wk, new_val);

	if ((t1 & t2) != t2) {
		char buf[BUF_SIZE_2k] = { 0 };
		snprintf(buf,
			BUF_SIZE_2k,
			"reassignment of variable %s with type %s to conflicting type %s",
			a->name,
			typechecking_type_to_s(wk, t1),
			typechecking_type_to_s(wk, t2));

		if (new_a) {
			error_diagnostic_store_push(new_a->src_idx, new_a->location, log_warn, buf);
		} else {
			vm_warning_at(wk, n_id, "%s", buf);
		}
	}
}

static obj
push_assignment(struct workspace *wk, const char *name, obj o, uint32_t ip)
{
	// initialize source location to 0 since some variables don't have
	// anything to put there, like builtin variables
	uint32_t src_idx, ep_stack_len = 0, ep_stacks_i = 0;
	copy_analyze_entrypoint_stack(&ep_stacks_i, &ep_stack_len);

	struct source_location loc;
	vm_lookup_inst_location_src_idx(&wk->vm, ip, &loc, &src_idx);

	// Add the new assignment to the current scope and return its index as
	// an obj (for storage in the scope dict)
	obj v = assignments.len;
	bucket_arr_push(&assignments,
		&(struct assignment){
			.name = name,
			.o = o,
			.location = loc,
			.src_idx = src_idx,
			.ep_stacks_i = ep_stacks_i,
			.ep_stack_len = ep_stack_len,
		});
	return v;
}

static struct assignment *
scope_assign(struct workspace *wk, const char *name, obj o, uint32_t n_id, enum variable_assignment_mode mode)
{
	TracyCZoneAutoS;
	obj scope = 0;
	if (mode == assign_reassign) {
		if (!(scope = assign_lookup_scope(wk, name))) {
			mode = assign_local;
		}
	}

	if (mode == assign_local) {
		obj local_scope = obj_array_get_tail(wk, wk->vm.scope_stack);
		if (get_obj_array(wk, local_scope)->len == 1) {
			scope = obj_array_get_tail(wk, local_scope);
		} else {
			obj scope_group = obj_array_get_tail(wk, local_scope);
			scope = obj_array_get_tail(wk, scope_group);
		}
	}

	assert(scope);

	struct assignment *a = 0;
	if (analyzer.impure_loop_depth && (a = assign_lookup(wk, name))) {
		// When overwriting a variable in a loop turn it into a
		// typeinfo so that it gets marked as impure.
		enum obj_type new_type = get_obj_type(wk, o);
		if (new_type != obj_typeinfo && !obj_equal(wk, a->o, o)) {
			o = make_typeinfo(wk, obj_type_to_tc_type(new_type));
		}
	}

	obj aid;
	if (obj_dict_index(wk, scope, make_str(wk, name), &aid)) {
		// The assignment was found so just reassign it here
		a = bucket_arr_get(&assignments, aid);
		check_reassign_to_different_type(wk, a, o, NULL, n_id);

		a->o = o;
		TracyCZoneAutoE;
		return a;
	}

	aid = push_assignment(wk, name, o, n_id);
	obj_dict_set(wk, scope, make_str(wk, name), aid);

	TracyCZoneAutoE;
	return bucket_arr_get(&assignments, aid);
}

static void
push_scope_group(struct workspace *wk)
{
	obj local_scope = obj_array_get_tail(wk, wk->vm.scope_stack), scope_group;
	make_obj(wk, &scope_group, obj_array);
	obj_array_push(wk, local_scope, scope_group);
}

static void
push_scope_group_scope(struct workspace *wk)
{
	obj local_scope = obj_array_get_tail(wk, wk->vm.scope_stack), scope_group = obj_array_get_tail(wk, local_scope),
	    scope;

	make_obj(wk, &scope, obj_dict);
	obj_array_push(wk, scope_group, scope);
}

static void
merge_objects(struct workspace *wk, struct assignment *dest, struct assignment *src)
{
	type_tag dest_type = get_obj_type(wk, dest->o);
	type_tag src_type = get_obj_type(wk, src->o);

	src->accessed = true;

	if (dest_type != obj_typeinfo) {
		dest->o = make_typeinfo(wk, obj_type_to_tc_type(dest_type));
	}

	if (src_type != obj_typeinfo) {
		src->o = make_typeinfo(wk, obj_type_to_tc_type(src_type));
	}

	check_reassign_to_different_type(wk, dest, src->o, src, 0);
	merge_types(wk, get_obj_typeinfo(wk, dest->o), src->o);

	assert(get_obj_type(wk, dest->o) == obj_typeinfo);
	assert(get_obj_type(wk, src->o) == obj_typeinfo);
	src->o = 0;
}

struct pop_scope_group_ctx {
	uint32_t i;
	obj merged, base;
};

static enum iteration_result
merge_scope_group_scope_iter(struct workspace *wk, void *_ctx, obj k, obj aid)
{
	struct pop_scope_group_ctx *ctx = _ctx;

	obj bid;
	struct assignment *b, *a = bucket_arr_get(&assignments, aid);
	if (obj_dict_index(wk, ctx->merged, k, &bid)) {
		b = bucket_arr_get(&assignments, bid);
		merge_objects(wk, b, a);
	} else {
		obj_dict_set(wk, ctx->merged, k, aid);
	}

	return ir_cont;
}

static enum iteration_result
merge_scope_group_scopes_iter(struct workspace *wk, void *_ctx, obj scope)
{
	struct pop_scope_group_ctx *ctx = _ctx;
	if (ctx->i == 0) {
		goto cont;
	}

	obj_dict_foreach(wk, scope, ctx, merge_scope_group_scope_iter);
cont:
	++ctx->i;
	return ir_cont;
}

static enum iteration_result
merge_scope_group_scope_with_base_iter(struct workspace *wk, void *_ctx, obj k, obj aid)
{
	struct assignment *a = bucket_arr_get(&assignments, aid), *b;
	if ((b = assign_lookup(wk, a->name))) {
		merge_objects(wk, b, a);
	} else {
		type_tag type = get_obj_type(wk, a->o);
		if (type != obj_typeinfo) {
			a->o = make_typeinfo(wk, obj_type_to_tc_type(type));
		}

		b = scope_assign(wk, a->name, a->o, 0, assign_local);
		b->accessed = a->accessed;
		b->location = a->location;
		b->src_idx = a->src_idx;

		a->accessed = true;
	}

	return ir_cont;
}

static void
pop_scope_group(struct workspace *wk)
{
	obj local_scope = obj_array_get_tail(wk, wk->vm.scope_stack);

	if (get_obj_array(wk, local_scope)->len == 1) {
		return;
	}

	obj scope_group = obj_array_pop(wk, local_scope);

	struct pop_scope_group_ctx ctx = { 0 };
	obj_array_index(wk, scope_group, 0, &ctx.merged);

	if (get_obj_array(wk, local_scope)->len == 1) {
		obj_array_index(wk, local_scope, 0, &ctx.base);
	} else {
		obj prev_scope_group = obj_array_get_tail(wk, local_scope);
		ctx.base = obj_array_get_tail(wk, prev_scope_group);
	}

	obj_array_foreach(wk, scope_group, &ctx, merge_scope_group_scopes_iter);

	obj_dict_foreach(wk, ctx.merged, &ctx, merge_scope_group_scope_with_base_iter);
}

/******************************************************************************
 * analyzer behavior functions
 ******************************************************************************/

static void
analyze_assign_wrapper(struct workspace *wk, const char *name, obj o, uint32_t n_id, enum variable_assignment_mode mode)
{
	scope_assign(wk, name, o, n_id, mode);
}

static bool
analyze_lookup_wrapper(struct workspace *wk, const char *name, obj *res)
{
	struct assignment *a = assign_lookup(wk, name);
	if (a) {
		a->accessed = true;
		*res = a->o;
		return true;
	} else {
		return false;
	}
}

static void
analyze_push_local_scope(struct workspace *wk)
{
	obj scope_group;
	make_obj(wk, &scope_group, obj_array);
	obj scope;
	make_obj(wk, &scope, obj_dict);
	obj_array_push(wk, scope_group, scope);
	obj_array_push(wk, wk->vm.scope_stack, scope_group);
}

static void
analyze_pop_local_scope(struct workspace *wk)
{
	obj scope_group = obj_array_pop(wk, wk->vm.scope_stack);
	assert(get_obj_array(wk, scope_group)->len == 1);
}

static enum iteration_result
analyze_scope_stack_scope_dup_iter(struct workspace *wk, void *_ctx, obj v)
{
	obj *g = _ctx;
	obj scope;
	obj_dict_dup(wk, v, &scope);
	obj_array_push(wk, *g, scope);
	return ir_cont;
}

static enum iteration_result
analyze_scope_stack_dup_iter(struct workspace *wk, void *_ctx, obj scope_group)
{
	obj *r = _ctx;
	obj g;
	make_obj(wk, &g, obj_array);
	obj_array_foreach(wk, scope_group, &g, analyze_scope_stack_scope_dup_iter);
	obj_array_push(wk, *r, g);
	return ir_cont;
}

static obj
analyze_scope_stack_dup(struct workspace *wk, obj scope_stack)
{
	obj r;
	make_obj(wk, &r, obj_array);

	obj_array_foreach(wk, scope_stack, &r, analyze_scope_stack_dup_iter);
	return r;
}

static bool
analyze_eval_project_file(struct workspace *wk, const char *path, bool first)
{
	const char *newpath = path;
	if (analyzer.opts->file_override && strcmp(analyzer.opts->file_override, path) == 0) {
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

// HACK: This works like disabler_among_args kind of.  These opts should only
// ever be set by analyze_function().
/* static struct analyze_function_opts { */
/* 	bool do_analyze; */
/* 	bool pure_function; */
/* 	bool encountered_error; */
/* 	bool allow_impure_args, allow_impure_args_except_first; // set to true for set_variable and subdir */

/* 	bool dump_signature; // used when dumping funciton signatures */
/* } analyze_function_opts; */

static bool
analyze_native_func_dispatch(struct workspace *wk, uint32_t func_idx, obj self, obj *res)
{
	if (!native_funcs[func_idx].pure) {
		*res = make_typeinfo(wk, native_funcs[func_idx].return_type);
		L("skipping call %s", native_funcs[func_idx].name);
		return true;
	}

	L("about to call %s", native_funcs[func_idx].name);
	return native_funcs[func_idx].func(wk, self, res);
}

#if 0
bool
analyze_function(struct workspace *wk,
	const struct func_impl *fi,
	uint32_t args_node,
	obj self,
	obj *res,
	bool *was_pure)
{
	struct analyze_function_opts old_opts = analyze_function_opts;
	*res = 0;

	bool pure = fi->pure;

	struct obj_tainted_by_typeinfo_ctx tainted_ctx = { .allow_tainted_dict_values = true };
	if (self && obj_tainted_by_typeinfo(wk, self, &tainted_ctx)) {
		pure = false;
	}

	if (!self) {
		if (strcmp(fi->name, "set_variable") == 0 || strcmp(fi->name, "subdir") == 0) {
			analyze_function_opts.allow_impure_args_except_first = true;
		} else if (strcmp(fi->name, "p") == 0) {
			analyze_function_opts.allow_impure_args = true;
		}
	}

	analyze_function_opts.do_analyze = true;
	// pure_function can be set to false even if it was true in the case
	// that any of its arguments are of type obj_typeinfo
	analyze_function_opts.pure_function = pure;
	analyze_function_opts.encountered_error = true;

	bool func_ret = fi->func(wk, self, args_node, res);

	pure = analyze_function_opts.pure_function;
	bool ok = !analyze_function_opts.encountered_error;

	analyze_function_opts = old_opts;

	*was_pure = pure;

	if (pure) {
		return func_ret;
	} else {
		return ok;
	}
	return false;
}
#endif

static bool
analyze_pop_args(struct workspace *wk, struct args_norm an[], struct args_kw akw[])
{
	return vm_pop_args(wk, an, akw);
}

/******************************************************************************
 * eval trace helpers
 ******************************************************************************/

struct eval_trace_print_ctx {
	uint32_t indent, len, i;
	uint64_t bars;
};

static enum iteration_result
eval_trace_arr_len_iter(struct workspace *wk, void *_ctx, obj v)
{
	if (get_obj_type(wk, v) != obj_array) {
		++*(uint32_t *)_ctx;
	}
	return ir_cont;
}
static uint32_t
eval_trace_arr_len(struct workspace *wk, obj arr)
{
	uint32_t cnt = 0;
	obj_array_foreach(wk, arr, &cnt, eval_trace_arr_len_iter);
	return cnt;
}

static enum iteration_result
eval_trace_print(struct workspace *wk, void *_ctx, obj v)
{
	struct eval_trace_print_ctx *ctx = _ctx;
	switch (get_obj_type(wk, v)) {
	case obj_array: {
		struct eval_trace_print_ctx subctx = {
			.indent = ctx->indent + 1,
			.bars = ctx->bars,
			.len = eval_trace_arr_len(wk, v),
		};
		if (ctx->i <= ctx->len - 1) {
			subctx.bars |= (1 << (ctx->indent - 1));
		}
		obj_array_foreach(wk, v, &subctx, eval_trace_print);
		break;
	}
	case obj_string: {
		uint32_t i;
		for (i = 0; i < ctx->indent; ++i) {
			if (i < ctx->indent - 1) {
				if (ctx->bars & (1 << i)) {
					printf("│   ");
				} else {
					printf("    ");
				}
			} else if (ctx->i == ctx->len - 1) {
				printf("└── ");
			} else {
				printf("├── ");
			}
		}

		SBUF(rel);
		if (path_is_absolute(get_cstr(wk, v))) {
			SBUF(cwd);
			path_copy_cwd(wk, &cwd);
			path_relative_to(wk, &rel, cwd.buf, get_cstr(wk, v));
			printf("%s\n", rel.buf);
		} else {
			printf("%s\n", get_cstr(wk, v));
		}
		++ctx->i;
		break;
	}
	default: UNREACHABLE;
	}
	return ir_cont;
}

/******************************************************************************
 * analyzer options
 ******************************************************************************/

static const struct {
	const char *name;
	enum analyze_diagnostic d;
} analyze_diagnostic_names[] = {
	{ "unused-variable", analyze_diagnostic_unused_variable },
	{ "reassign-to-conflicting-type", analyze_diagnostic_reassign_to_conflicting_type },
	{ "dead-code", analyze_diagnostic_dead_code },
	{ "redirect-script-error", analyze_diagnostic_redirect_script_error },
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

/******************************************************************************
 * entrypoint
 ******************************************************************************/

static enum iteration_result
reassign_default_var(struct workspace *wk, void *_ctx, obj k, obj v)
{
	obj *scope = _ctx;
	obj aid = push_assignment(wk, get_cstr(wk, k), v, 0);

	struct assignment *a = bucket_arr_get(&assignments, aid);
	a->default_var = true;

	obj_dict_set(wk, *scope, k, aid);
	return ir_cont;
}

bool
do_analyze(struct analyze_opts *opts)
{
	bool res = false;
	analyzer.opts = opts;
	struct workspace wk;
	workspace_init(&wk);

	bucket_arr_init(&assignments, 512, sizeof(struct assignment));
	{ /* re-initialize the default scope */
		obj default_scope_stack, scope_group, scope;
		obj_array_index(&wk, wk.vm.default_scope_stack, 0, &default_scope_stack);
		make_obj(&wk, &wk.vm.default_scope_stack, obj_array);
		make_obj(&wk, &scope_group, obj_array);
		make_obj(&wk, &scope, obj_dict);
		obj_array_push(&wk, scope_group, scope);
		obj_array_push(&wk, wk.vm.default_scope_stack, scope_group);
		obj_dict_foreach(&wk, default_scope_stack, &scope, reassign_default_var);
	}

	wk.vm.behavior.assign_variable = analyze_assign_wrapper;
	wk.vm.behavior.unassign_variable = analyze_unassign;
	wk.vm.behavior.push_local_scope = analyze_push_local_scope;
	wk.vm.behavior.pop_local_scope = analyze_pop_local_scope;
	wk.vm.behavior.get_variable = analyze_lookup_wrapper;
	wk.vm.behavior.scope_stack_dup = analyze_scope_stack_dup;
	wk.vm.behavior.eval_project_file = analyze_eval_project_file;
	wk.vm.behavior.native_func_dispatch = analyze_native_func_dispatch;
	wk.vm.behavior.pop_args = analyze_pop_args;
	/* wk.vm.behavior.lookup_method = analyze_lookup_method; */
	wk.vm.in_analyzer = true;

	analyzer.unpatched_ops = wk.vm.ops;

	error_diagnostic_store_init(&wk);

	arr_init(&analyze_entrypoint_stack, 32, sizeof(struct analyze_file_entrypoint));
	arr_init(&analyze_entrypoint_stacks, 32, sizeof(struct analyze_file_entrypoint));

	if (analyzer.opts->eval_trace) {
		make_obj(&wk, &wk.vm.dbg_state.eval_trace, obj_array);
	}

	if (analyzer.opts->file_override) {
		const char *root = determine_project_root(&wk, analyzer.opts->file_override);
		if (root) {
			SBUF(cwd);
			path_copy_cwd(&wk, &cwd);

			if (strcmp(cwd.buf, root) != 0) {
				path_chdir(root);
				wk.source_root = root;
			}
		}
	}

	if (opts->internal_file) {
		struct assignment *a = scope_assign(&wk, "argv", make_typeinfo(&wk, tc_array), 0, assign_local);
		a->default_var = true;

		wk.vm.lang_mode = language_extended;

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
		L(">>>");
		res = eval_project(&wk, NULL, wk.source_root, wk.build_root, &project_id);
		L("<<<");
	}

	if (analyze_diagnostic_enabled(analyze_diagnostic_unused_variable)) {
		uint32_t i;
		for (i = 0; i < assignments.len; ++i) {
			struct assignment *a = bucket_arr_get(&assignments, i);
			if (!a->default_var && !a->accessed && *a->name != '_') {
				const char *msg = get_cstr(&wk, make_strf(&wk, "unused variable %s", a->name));
				enum log_level lvl = log_warn;
				error_diagnostic_store_push(a->src_idx, a->location, lvl, msg);

				if (analyzer.opts->subdir_error && a->ep_stack_len) {
					mark_analyze_entrypoint_as_containing_diagnostic(a->ep_stacks_i, lvl);
				}
			}
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
					ep_stack[j].src_idx, ep_stack[j].location, lvl, "in subdir");
			}
		}

		i += len;
	}

	bool saw_error;
	if (analyzer.opts->eval_trace) {
		struct eval_trace_print_ctx ctx = {
			.indent = 1,
			.len = eval_trace_arr_len(&wk, wk.vm.dbg_state.eval_trace),
		};
		obj_array_foreach(&wk, wk.vm.dbg_state.eval_trace, &ctx, eval_trace_print);
	} else if (analyzer.opts->get_definition_for) {
		bool found = false;
		uint32_t i;
		for (i = 0; i < assignments.len; ++i) {
			struct assignment *a = bucket_arr_get(&assignments, i);
			if (strcmp(a->name, analyzer.opts->get_definition_for) == 0) {
				struct source *src = arr_get(&wk.vm.src, a->src_idx);
				list_line_range(src, a->location, 1);
				found = true;
			}
		}

		if (!found) {
			LOG_W("couldn't find definition for %s", analyzer.opts->get_definition_for);
		}
	} else {
		error_diagnostic_store_replay(analyzer.opts->replay_opts, &saw_error);

		if (saw_error || analyzer.error) {
			res = false;
		}
	}

	bucket_arr_destroy(&assignments);
	arr_destroy(&analyze_entrypoint_stack);
	arr_destroy(&analyze_entrypoint_stacks);
	workspace_destroy(&wk);
	return res;
}
