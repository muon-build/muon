/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "lang/analyze.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "tracy.h"

static struct {
	const struct az_opts *opts;
	struct obj_func *fp;
	obj eval_trace;
	uint32_t impure_loop_depth;
	bool error;

	struct vm_ops unpatched_ops;
	struct obj_typeinfo az_injected_native_func_return;
	struct hash branch_map;
} analyzer;

struct az_file_entrypoint {
	bool is_root, has_diagnostic;
	enum log_level lvl;
	struct source_location location;
	uint32_t src_idx;
};

static struct arr az_entrypoint_stack, az_entrypoint_stacks;

struct assignment {
	const char *name;
	obj o;
	bool accessed, default_var;
	struct source_location location;
	uint32_t src_idx;

	uint32_t ep_stacks_i;
	uint32_t ep_stack_len;
};

enum branch_map_type {
	branch_map_type_normal,
	branch_map_type_ternary,
};

union branch_map {
	struct branch_map_data {
		uint8_t taken, not_taken, impure, type;
	} data;
	uint64_t u64;
};

struct bucket_arr assignments;

void
az_set_error(void)
{
	analyzer.error = true;
}

/******************************************************************************
 * typeinfo utilities
 ******************************************************************************/

struct obj_tainted_by_typeinfo_ctx {
	bool allow_tainted_dict_values;
};

static bool obj_tainted_by_typeinfo(struct workspace *wk, obj o, struct obj_tainted_by_typeinfo_ctx *ctx);

static enum iteration_result
obj_tainted_by_typeinfo_dict_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	struct obj_tainted_by_typeinfo_ctx *ctx = _ctx;
	if (obj_tainted_by_typeinfo(wk, k, 0)) {
		return ir_err;
	}

	if (ctx && !ctx->allow_tainted_dict_values && obj_tainted_by_typeinfo(wk, v, 0)) {
		return ir_err;
	}

	return ir_cont;
}

static enum iteration_result
obj_tainted_by_typeinfo_array_iter(struct workspace *wk, void *_ctx, obj v)
{
	if (obj_tainted_by_typeinfo(wk, v, _ctx)) {
		return ir_err;
	}

	return ir_cont;
}

static bool
obj_tainted_by_typeinfo(struct workspace *wk, obj o, struct obj_tainted_by_typeinfo_ctx *ctx)
{
	if (!o) {
		return true;
	}

	switch (get_obj_type(wk, o)) {
	case obj_typeinfo: return true;
	case obj_array: return !obj_array_foreach(wk, o, ctx, obj_tainted_by_typeinfo_array_iter);
	case obj_dict: return !obj_dict_foreach(wk, o, ctx, obj_tainted_by_typeinfo_dict_iter);
	default: return false;
	}
}

static void
copy_az_entrypoint_stack(uint32_t *ep_stacks_i, uint32_t *ep_stack_len)
{
	if (az_entrypoint_stack.len) {
		*ep_stacks_i = az_entrypoint_stacks.len;
		arr_grow_by(&az_entrypoint_stacks, az_entrypoint_stack.len);
		*ep_stack_len = az_entrypoint_stack.len;

		memcpy(arr_get(&az_entrypoint_stacks, *ep_stacks_i),
			az_entrypoint_stack.e,
			sizeof(struct az_file_entrypoint) * az_entrypoint_stack.len);
	} else {
		*ep_stacks_i = 0;
		*ep_stack_len = 0;
	}
}

static void
mark_az_entrypoint_as_containing_diagnostic(uint32_t ep_stacks_i, enum log_level lvl)
{
	struct az_file_entrypoint *ep = arr_get(&az_entrypoint_stacks, ep_stacks_i);

	ep->has_diagnostic = true;
	ep->lvl = lvl;
}

static bool
az_diagnostic_enabled(enum az_diagnostic d)
{
	return analyzer.opts->enabled_diagnostics & d;
}

/*static*/ const char *
inspect_typeinfo(struct workspace *wk, obj t)
{
	if (get_obj_type(wk, t) == obj_typeinfo) {
		struct obj_typeinfo *ti = get_obj_typeinfo(wk, t);

		return typechecking_type_to_s(wk, ti->type);
	} else {
		return obj_type_to_s(get_obj_type(wk, t));
	}
}

static type_tag
flatten_type(struct workspace *wk, type_tag t)
{
	if (!(t & TYPE_TAG_COMPLEX)) {
		t &= ~TYPE_TAG_GLOB;

		if (t & TYPE_TAG_LISTIFY) {
			t = tc_array;
		}
		return t;
	}

	uint32_t idx = COMPLEX_TYPE_INDEX(t);
	enum complex_type ct = COMPLEX_TYPE_TYPE(t);

	struct bucket_arr *typeinfo_arr = &wk->vm.objects.obj_aos[obj_typeinfo - _obj_aos_start];
	struct obj_typeinfo *ti = bucket_arr_get(typeinfo_arr, idx);

	switch (ct) {
	case complex_type_or: return flatten_type(wk, ti->type) | flatten_type(wk, ti->subtype);
	case complex_type_nested: return flatten_type(wk, ti->type);
	}

	UNREACHABLE_RETURN;
}

obj
make_typeinfo(struct workspace *wk, type_tag t)
{
	obj res;
	make_obj(wk, &res, obj_typeinfo);
	struct obj_typeinfo *type = get_obj_typeinfo(wk, res);
	type->type = t;
	return res;
}

obj
make_az_branch_element(struct workspace *wk, uint32_t ip, uint32_t flags)
{
	union az_branch_element elem = {
		.data = {
			.ip = ip,
			.flags = flags,
		},
	};
	return make_number(wk, elem.i64);
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

struct az_ctx {
	type_tag expected;
	type_tag found;
	const struct func_impl *found_func;
	obj found_func_obj, found_func_module;
	struct obj_typeinfo ti;
	obj l;
};

typedef void((az_for_each_type_cb)(struct workspace *wk, struct az_ctx *ctx, uint32_t n_id, type_tag t, obj *res));

/*static*/ void
az_for_each_type(struct workspace *wk,
	struct az_ctx *ctx,
	uint32_t n_id,
	obj o,
	type_tag typemask,
	az_for_each_type_cb cb,
	obj *res)
{
	obj r = 0;

	type_tag t;
	if ((t = get_obj_type(wk, o)) == obj_typeinfo) {
		t = get_obj_typeinfo(wk, o)->type;
	}

	if (t & obj_typechecking_type_tag) {
		if (t == tc_disabler) {
			vm_warning_at(wk, n_id, "this expression is always disabled");
			ctx->expected = tc_any;
			*res = make_typeinfo(wk, tc_disabler);
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

/******************************************************************************
 * scope handling
 ******************************************************************************/

/*
 * Variable assignment and retrieval is handled with the following functions.
 * The additional complexity is required due to variables that are
 * conditionally assigned, e.g. assigned in an if-block, or foreach loop.
 *
 * When a conditional block is started, a "scope group" is created, and then
 * every branch of the if statement gets its own sub-scope.  At the end of the
 * if statement, all the sub scopes are merged (conflicting object types get
 * merged) into the parent scope, and the scope group is popped.
 *
 * The scope stack, rather than simply an array of dicts as in normal vm
 * execution, is an array of "scope groups".  The first group is a plain dict,
 * which represents the root scope for this level in the scope stack.  When
 * entering a branch, additional groups are pushed onto this list each
 * consisting of a separate dict per sub-scope.
 */

void
print_scope_stack(struct workspace *wk)
{
	L("scope stack:");
	obj local_scope, scope_group, scope, k, v;
	obj_array_for(wk, wk->vm.scope_stack, local_scope) {
		L("  local scope:");
		uint32_t i = 0;
		obj_array_for(wk, local_scope, scope_group) {
			if (i == 0) {
				L("    root scope:");
				obj_dict_for(wk, scope_group, k, v) {
					struct assignment *assign = bucket_arr_get(&assignments, v);
					LO("      %o: %s %o\n", k, assign->accessed ? "a" : " ", assign->o);
				}
			} else {
				obj_array_for(wk, scope_group, scope) {
					struct assignment *assign = bucket_arr_get(&assignments, v);
					L("    scope group:");
					obj_dict_for(wk, scope, k, v) {
						LO("      %o: %s %o\n", k, assign->accessed ? "a" : " ", assign->o);
					}
				}
			}

			++i;
		}
	}
}

struct check_az_scope_ctx {
	const char *name;
	uint32_t i;
	obj res, scope;
	bool found;
};

// example local_scope: [{a: 1}, [{b: 2}, {b: 3}], [{c: 4}]]
static enum iteration_result
az_check_scope_stack(struct workspace *wk, void *_ctx, obj local_scope)
{
	TracyCZoneAutoS;
	struct check_az_scope_ctx *ctx = _ctx;

	obj base;
	obj_array_index(wk, local_scope, 0, &base);

	uint32_t local_scope_len = get_obj_array(wk, local_scope)->len;
	if (local_scope_len > 1) {
		int32_t i;
		// example: {a: 1},           -- skip
		// example: [{b: 2}, {b: 3}], -- take last
		// example: [{c: 4}]          -- take last
		for (i = local_scope_len - 1; i >= 1; --i) {
			struct check_az_scope_ctx *ctx = _ctx;

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
	struct check_az_scope_ctx ctx = { .name = name };
	obj_array_foreach(wk, wk->vm.scope_stack, &ctx, az_check_scope_stack);

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
	struct check_az_scope_ctx ctx = { .name = name };
	obj_array_foreach(wk, wk->vm.scope_stack, &ctx, az_check_scope_stack);

	if (ctx.found) {
		TracyCZoneAutoE;
		return ctx.scope;
	} else {
		TracyCZoneAutoE;
		return 0;
	}
}

static void
az_unassign(struct workspace *wk, const char *name)
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
	if (!az_diagnostic_enabled(az_diagnostic_reassign_to_conflicting_type)) {
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
	copy_az_entrypoint_stack(&ep_stacks_i, &ep_stack_len);

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
	bool accessed = false;

	if (mode == assign_reassign) {
		mode = assign_local;
		accessed = true;
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
		// When overwriting a variable in an impure loop turn it into a
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

	struct assignment *assign = bucket_arr_get(&assignments, aid);
	assign->accessed = accessed;

	TracyCZoneAutoE;
	return assign;
}

static void
push_scope_group(struct workspace *wk)
{
	obj local_scope = obj_array_get_tail(wk, wk->vm.scope_stack);
	obj scope_group;
	make_obj(wk, &scope_group, obj_array);
	obj_array_push(wk, local_scope, scope_group);
}

static void
push_scope_group_scope(struct workspace *wk)
{
	obj local_scope = obj_array_get_tail(wk, wk->vm.scope_stack);
	obj scope_group = obj_array_get_tail(wk, local_scope);
	obj scope;

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
 * analyzer ops
 ******************************************************************************/

struct az_branch_group {
	enum az_branch_type type;
	bool loop_impure;
	uint32_t merge_point; // TODO: remove this, it is unused

	struct az_branch_element_data branch;
	struct branch_map_data result;
};

static struct az_branch_group cur_branch_group;

static void
az_op_az_branch(struct workspace *wk)
{
	enum az_branch_type branch_type = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	uint32_t merge_point = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	{
		struct az_branch_group new_branch_group = {
			.merge_point = merge_point,
			.type = branch_type,
		};
		stack_push(&wk->stack, cur_branch_group, new_branch_group);
	}

	if (branch_type == az_branch_type_loop) {
		obj it = object_stack_peek(&wk->vm.stack, 1);
		if (get_obj_iterator(wk, it)->type == obj_iterator_type_typeinfo) {
			++analyzer.impure_loop_depth;
			cur_branch_group.loop_impure = true;
		}
		return;
	}

	uint32_t branches = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	push_scope_group(wk);

	L("---> branching, merging @ %03x", cur_branch_group.merge_point);

	bool pure = true;
	obj branch, expr_result = 0;
	obj_array_for(wk, branches, branch) {
		L("--> branch");
		cur_branch_group.branch = (union az_branch_element){ .i64 = get_obj_number(wk, branch) }.data;
		cur_branch_group.result = (struct branch_map_data){ 0 };
		wk->vm.ip = cur_branch_group.branch.ip;
		arr_push(&wk->vm.call_stack, &(struct call_frame){ .type = call_frame_type_eval });
		push_scope_group_scope(wk);
		vm_execute(wk);

		if (cur_branch_group.result.impure) {
			pure = false;
		}
		if (pure && cur_branch_group.result.taken) {
			break;
		}

		if ((cur_branch_group.branch.flags & az_branch_element_flag_pop)
			&& (cur_branch_group.result.taken || !pure)) {
			struct obj_stack_entry *entry = object_stack_pop_entry(&wk->vm.stack);
			type_tag entry_type = coerce_type_tag(wk, entry->o);

			if (!expr_result) {
				expr_result = make_typeinfo(wk, entry_type);
			} else {
				merge_types(wk, get_obj_typeinfo(wk, expr_result), entry->o);
			}
		}
	}

	L("<--- all branches merged %03x <---", cur_branch_group.merge_point);

	pop_scope_group(wk);

	stack_pop(&wk->stack, cur_branch_group);

	if (expr_result && !pure) {
		object_stack_push(wk, expr_result);
	}
}

static void
az_op_az_merge(struct workspace *wk)
{
	L("<--- joining branch %03x, %03x", cur_branch_group.merge_point, wk->vm.ip - 1);

	if (cur_branch_group.type == az_branch_type_loop) {
		if (cur_branch_group.loop_impure) {
			--analyzer.impure_loop_depth;
		}

		stack_pop(&wk->stack, cur_branch_group);
		return;
	}

	struct call_frame *frame = arr_pop(&wk->vm.call_stack);

	assert(cur_branch_group.merge_point == wk->vm.ip - 1);
	assert(frame->type == call_frame_type_eval);

	object_stack_push(wk, 0);
	wk->vm.run = false;
}

static void
az_jmp_if_cond_matches(struct workspace *wk, bool cond)
{
	struct obj_stack_entry *entry = object_stack_pop_entry(&wk->vm.stack);
	vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	typecheck(wk, entry->ip, entry->o, obj_bool);

	union branch_map *map;
	{
		uint32_t ip = wk->vm.ip - 1;
		if (!(map = (union branch_map *)hash_get(&analyzer.branch_map, &ip))) {
			hash_set(&analyzer.branch_map, &ip, 0);
			map = (union branch_map *)hash_get(&analyzer.branch_map, &ip);
		}
	}

	if (cur_branch_group.branch.flags & az_branch_element_flag_pop) {
		map->data.type = branch_map_type_ternary;
	}

	if (get_obj_type(wk, entry->o) == obj_bool) {
		if (cond == get_obj_bool(wk, entry->o)) {
			map->data.not_taken = cur_branch_group.result.not_taken = true;
			wk->vm.ip = cur_branch_group.merge_point;
		} else {
			map->data.taken = cur_branch_group.result.taken = true;
		}
	} else {
		map->data.impure = cur_branch_group.result.impure = true;
	}
}

static void
az_op_jmp_if_false(struct workspace *wk)
{
	az_jmp_if_cond_matches(wk, false);
}

static void
az_op_jmp_if_true(struct workspace *wk)
{
	az_jmp_if_cond_matches(wk, true);
}

static void
az_op_jmp_if_disabler(struct workspace *wk)
{
	vm_get_constant(wk->vm.code.e, &wk->vm.ip);
}

static void
az_op_jmp_if_disabler_keep(struct workspace *wk)
{
	vm_get_constant(wk->vm.code.e, &wk->vm.ip);
}

struct az_func_context {
	struct obj_capture *capture;
};

static struct az_func_context cur_func_context;

static void
az_op_return(struct workspace *wk)
{
	if (cur_func_context.capture) {
		obj v = object_stack_peek(&wk->vm.stack, 1);
		typecheck_custom(
			wk, 0, v, cur_func_context.capture->func->return_type, "expected return type %s, got %s");
	}
}

static void
az_op_return_end(struct workspace *wk)
{
	struct call_frame *frame = arr_peek(&wk->vm.call_stack, 1);

	if (frame->type == call_frame_type_func) {
		object_stack_pop(&wk->vm.stack);
		object_stack_push(wk, make_typeinfo(wk, flatten_type(wk, cur_func_context.capture->func->return_type)));
	}

	analyzer.unpatched_ops.ops[op_return_end](wk);
}

static void
az_op_add_store(struct workspace *wk)
{
	obj a, b;

	b = object_stack_pop(&wk->vm.stack);
	obj a_id = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	const struct str *id = get_str(wk, a_id);
	if (!wk->vm.behavior.get_variable(wk, id->s, &a)) {
		vm_error(wk, "undefined object %s", get_cstr(wk, a_id));
		object_stack_push(wk, make_typeinfo(wk, tc_any));
		return;
	}

	object_stack_push(wk, a);
	object_stack_push(wk, b);
	analyzer.unpatched_ops.ops[op_add](wk);

	obj res = object_stack_peek(&wk->vm.stack, 1);
	wk->vm.behavior.assign_variable(wk, id->s, res, 0, assign_reassign);
}

/******************************************************************************
 * analyzer behaviors
 ******************************************************************************/

static void
az_assign_wrapper(struct workspace *wk, const char *name, obj o, uint32_t n_id, enum variable_assignment_mode mode)
{
	scope_assign(wk, name, o, n_id, mode);
}

static bool
az_lookup_wrapper(struct workspace *wk, const char *name, obj *res)
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
az_push_local_scope(struct workspace *wk)
{
	obj scope_group;
	make_obj(wk, &scope_group, obj_array);
	obj scope;
	make_obj(wk, &scope, obj_dict);
	obj_array_push(wk, scope_group, scope);
	obj_array_push(wk, wk->vm.scope_stack, scope_group);
}

static void
az_pop_local_scope(struct workspace *wk)
{
	obj scope_group = obj_array_pop(wk, wk->vm.scope_stack);
	assert(get_obj_array(wk, scope_group)->len == 1);
}

static enum iteration_result
az_scope_stack_scope_dup_iter(struct workspace *wk, void *_ctx, obj v)
{
	obj *g = _ctx;
	obj scope;
	obj_dict_dup(wk, v, &scope);
	obj_array_push(wk, *g, scope);
	return ir_cont;
}

static enum iteration_result
az_scope_stack_dup_iter(struct workspace *wk, void *_ctx, obj scope_group)
{
	obj *r = _ctx;
	obj g;
	make_obj(wk, &g, obj_array);
	obj_array_foreach(wk, scope_group, &g, az_scope_stack_scope_dup_iter);
	obj_array_push(wk, *r, g);
	return ir_cont;
}

static obj
az_scope_stack_dup(struct workspace *wk, obj scope_stack)
{
	obj r;
	make_obj(wk, &r, obj_array);

	obj_array_foreach(wk, scope_stack, &r, az_scope_stack_dup_iter);
	return r;
}

static bool
az_eval_project_file(struct workspace *wk, const char *path, bool first)
{
	const char *newpath = path;
	if (analyzer.opts->file_override && strcmp(analyzer.opts->file_override, path) == 0) {
		bool ret = false;
		struct source src = { 0 };
		if (!fs_read_entire_file("-", &src)) {
			return false;
		}
		src.label = get_cstr(wk, make_str(wk, path));

		obj res;
		if (!eval(wk, &src, first ? eval_mode_first : eval_mode_default, &res)) {
			goto ret;
		}

		ret = true;
ret:
		return ret;
	}

	return eval_project_file(wk, newpath, first);
}

/******************************************************************************
 * analyzer behaviors -- function lookup and dispatch
 ******************************************************************************/

struct az_pop_args_ctx {
	uint32_t id;
	bool do_analyze;
	bool pure_function;
	bool encountered_error;
	bool allow_impure_args;
	bool allow_impure_args_except_first; // set to true for set_variable and subdir
} pop_args_ctx;

static bool
az_injected_native_func(struct workspace *wk, obj self, obj *res)
{
	pop_args_ctx.encountered_error = false;

	// discard all arguments
	object_stack_discard(&wk->vm.stack, wk->vm.nargs + wk->vm.nkwargs * 2);

	*res = make_typeinfo(wk, analyzer.az_injected_native_func_return.type);
	return true;
}

static const struct func_impl az_func_impls[] = {
	{ "az_injected_native_func", az_injected_native_func },
	{ 0 },
};

struct func_impl_group az_func_impl_group = {
	.impls = az_func_impls,
};

static bool
az_func_lookup(struct workspace *wk, obj self, const char *name, uint32_t *idx, obj *func)
{
	// If self isn't typeinfo just proceed with standard func_lookup
	if (get_obj_type(wk, self) != obj_typeinfo) {
		return func_lookup(wk, self, name, idx, func);
	}

	struct obj_typeinfo res_t = { 0 }, *ti = get_obj_typeinfo(wk, self);
	struct {
		uint32_t idx;
		uint32_t matches;
	} lookup_res = { 0 };
	type_tag t = ti->type;
	uint32_t i;

	// Strip disabler from type list
	if ((t & tc_disabler) == tc_disabler) {
		t &= ~tc_disabler;
		t |= obj_typechecking_type_tag;
	}

	for (i = 1; i <= tc_type_count; ++i) {
		type_tag tc = obj_type_to_tc_type(i);
		if ((t & tc) != tc) {
			continue;
		}

		// TODO: add a warning if not all candidates match, e.g.
		// calling to_string on something that is potentially a string?
		if (func_lookup_for_group(func_impl_groups[i], wk->vm.lang_mode, name, &lookup_res.idx)) {
			++lookup_res.matches;

			res_t.type |= native_funcs[lookup_res.idx].return_type;
		}
	}

	if (lookup_res.matches < 1) {
		// No matches found
		return false;
	} else if (lookup_res.matches > 1) {
		// Multiple matches found, return the index of az_injected_native_func
		// and wire it up to return our merged return type.
		analyzer.az_injected_native_func_return = res_t;
		*idx = az_func_impl_group.off;
		*func = 0;
	} else {
		// Single match found, return it
		*idx = lookup_res.idx;
	}

	return true;
}

static bool
az_pop_args(struct workspace *wk, struct args_norm an[], struct args_kw akw[])
{
	if (!vm_pop_args(wk, an, akw)) {
		return false;
	}

	pop_args_ctx.encountered_error = false;

	if (!pop_args_ctx.do_analyze) {
		return true;
	}

	bool typeinfo_among_args = false;

	{ // Check if any argument value is tainted
		uint32_t i;
		for (i = 0; an && an[i].type != ARG_TYPE_NULL; ++i) {
			if (!an[i].set) {
				continue;
			}

			if (pop_args_ctx.allow_impure_args) {
				continue;
			} else if (pop_args_ctx.allow_impure_args_except_first && i > 0) {
				continue;
			}

			if (obj_tainted_by_typeinfo(wk, an[i].val, 0)) {
				typeinfo_among_args = true;
				break;
			}
		}

		if (!typeinfo_among_args && akw) {
			for (i = 0; akw[i].key; ++i) {
				if (!akw[i].set) {
					continue;
				}

				if (pop_args_ctx.allow_impure_args || pop_args_ctx.allow_impure_args_except_first) {
					continue;
				}

				if (obj_tainted_by_typeinfo(wk, akw[i].val, 0)) {
					typeinfo_among_args = true;
					break;
				}
			}
		}
	}

	if (typeinfo_among_args) {
		pop_args_ctx.pure_function = false;
	}

	if (pop_args_ctx.pure_function) {
		return true;
	}

	// now return false to halt the function
	return false;
}

static bool
az_native_func_dispatch(struct workspace *wk, uint32_t func_idx, obj self, obj *res)
{
	static uint32_t id = 0;
	struct az_pop_args_ctx _ctx = { .id = id };
	++id;
	stack_push(&wk->stack, pop_args_ctx, _ctx);

	*res = 0;
	bool pure = native_funcs[func_idx].pure;

	struct obj_tainted_by_typeinfo_ctx tainted_ctx = { .allow_tainted_dict_values = true };
	if (self && obj_tainted_by_typeinfo(wk, self, &tainted_ctx)) {
		pure = false;
	}

	if (!self) {
		if (strcmp(native_funcs[func_idx].name, "subdir") == 0) {
			pop_args_ctx.allow_impure_args_except_first = true;
		} else if (strcmp(native_funcs[func_idx].name, "p") == 0) {
			pop_args_ctx.allow_impure_args = true;
		}
	}

	pop_args_ctx.do_analyze = true;
	// pure_function can be set to false even if it was true in the case
	// that any of its arguments are of type obj_typeinfo
	pop_args_ctx.pure_function = pure;
	pop_args_ctx.encountered_error = true;

	bool func_ok = native_funcs[func_idx].func(wk, self, res);

	pure = pop_args_ctx.pure_function;

	bool args_ok = !pop_args_ctx.encountered_error;

	stack_pop(&wk->stack, pop_args_ctx);

	if (pure) {
		return func_ok;
	}

	if (func_idx == az_func_impl_group.off) {
		// This means the native function we called was
		// az_injected_native_func and we should respect the return
		// type.
	} else {
		*res = make_typeinfo(wk, native_funcs[func_idx].return_type);
	}

	if (!args_ok) {
		// Add error here if func was subdir
	}

	return args_ok;
}

static void
az_op_constant_func(struct workspace *wk)
{
	analyzer.unpatched_ops.ops[op_constant_func](wk);

	obj c = object_stack_peek(&wk->vm.stack, 1);
	struct obj_capture *capture = get_obj_capture(wk, c);

	struct args_norm an[ARRAY_LEN(capture->func->an)] = { 0 };
	struct args_kw akw[ARRAY_LEN(capture->func->akw)] = { 0 };
	{
		uint32_t i;
		for (i = 0; i < capture->func->nargs; ++i) {
			an[i].val = make_typeinfo(wk, flatten_type(wk, capture->func->an[i].type));
		}
		an[i].type = ARG_TYPE_NULL;

		for (i = 0; i < capture->func->nkwargs; ++i) {
			akw[i].key = capture->func->akw[i].key;
			akw[i].val = make_typeinfo(wk, flatten_type(wk, capture->func->akw[i].type));
		}
		akw[i].key = 0;
	}

	{
		obj res;
		struct az_func_context new_func_context = {
			.capture = capture,
		};

		stack_push(&wk->stack, pop_args_ctx, (struct az_pop_args_ctx){ 0 });
		stack_push(&wk->stack, cur_func_context, new_func_context);

		vm_eval_capture(wk, c, an, akw, &res);

		stack_pop(&wk->stack, cur_func_context);
		stack_pop(&wk->stack, pop_args_ctx);
	}
}

static void
az_op_call(struct workspace *wk)
{
	obj c = object_stack_peek(&wk->vm.stack, 1);
	bool pop_args_error;

	const struct az_pop_args_ctx new_pop_args_ctx = {
		.encountered_error = true,
	};
	stack_push(&wk->stack, pop_args_ctx, new_pop_args_ctx);
	analyzer.unpatched_ops.ops[op_call](wk);
	pop_args_error = pop_args_ctx.encountered_error;
	stack_pop(&wk->stack, pop_args_ctx);

	if (get_obj_type(wk, c) == obj_capture && !pop_args_error) {
		struct obj_capture *capture = get_obj_capture(wk, c);

		{
			// TODO: only if there were no errors!
			//
			// op_call just set some variables for function args
			// that we will never use but we don't want an unused
			// variable warning so mark them all accessed.
			obj local_scope = obj_array_get_tail(wk, wk->vm.scope_stack),
			    root_scope = obj_array_get_tail(wk, local_scope);
			obj _k, aid;
			obj_dict_for(wk, root_scope, _k, aid) {
				(void)_k;
				struct assignment *assign = bucket_arr_get(&assignments, aid);
				assign->accessed = true;
			}
		}

		object_stack_push(wk, make_typeinfo(wk, flatten_type(wk, capture->func->return_type)));
		analyzer.unpatched_ops.ops[op_return](wk);
	}
}

#if 0
bool
az_function(struct workspace *wk,
	const struct func_impl *fi,
	uint32_t args_node,
	obj self,
	obj *res,
	bool *was_pure)
{
	struct pop_args_ctx old_opts = pop_args_ctx;
	*res = 0;

	bool pure = fi->pure;

	struct obj_tainted_by_typeinfo_ctx tainted_ctx = { .allow_tainted_dict_values = true };
	if (self && obj_tainted_by_typeinfo(wk, self, &tainted_ctx)) {
		pure = false;
	}

	if (!self) {
		if (strcmp(fi->name, "set_variable") == 0 || strcmp(fi->name, "subdir") == 0) {
			pop_args_ctx.allow_impure_args_except_first = true;
		} else if (strcmp(fi->name, "p") == 0) {
			pop_args_ctx.allow_impure_args = true;
		}
	}

	pop_args_ctx.do_analyze = true;
	// pure_function can be set to false even if it was true in the case
	// that any of its arguments are of type obj_typeinfo
	pop_args_ctx.pure_function = pure;
	pop_args_ctx.encountered_error = true;

	bool func_ret = fi->func(wk, self, args_node, res);

	pure = pop_args_ctx.pure_function;
	bool ok = !pop_args_ctx.encountered_error;

	pop_args_ctx = old_opts;

	*was_pure = pure;

	if (pure) {
		return func_ret;
	} else {
		return ok;
	}
	return false;
}
#endif

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
	enum az_diagnostic d;
} az_diagnostic_names[] = {
	{ "unused-variable", az_diagnostic_unused_variable },
	{ "reassign-to-conflicting-type", az_diagnostic_reassign_to_conflicting_type },
	{ "dead-code", az_diagnostic_dead_code },
	{ "redirect-script-error", az_diagnostic_redirect_script_error },
};

bool
az_diagnostic_name_to_enum(const char *name, enum az_diagnostic *ret)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(az_diagnostic_names); ++i) {
		if (strcmp(az_diagnostic_names[i].name, name) == 0) {
			*ret = az_diagnostic_names[i].d;
			return true;
		}
	}

	return false;
}

void
az_print_diagnostic_names(void)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(az_diagnostic_names); ++i) {
		printf("%s\n", az_diagnostic_names[i].name);
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
do_analyze(struct az_opts *opts)
{
	bool res = false;
	analyzer.opts = opts;
	struct workspace wk;
	if (opts->internal_file) {
		workspace_init_bare(&wk);
	} else {
		workspace_init(&wk);
	}

	bucket_arr_init(&assignments, 512, sizeof(struct assignment));
	hash_init(&analyzer.branch_map, 1024, sizeof(uint32_t));

	{ /* re-initialize the default scope */
		obj original_scope, scope_group, scope;
		obj_array_index(&wk, wk.vm.default_scope_stack, 0, &original_scope);
		make_obj(&wk, &wk.vm.default_scope_stack, obj_array);
		make_obj(&wk, &scope_group, obj_array);
		make_obj(&wk, &scope, obj_dict);
		obj_array_push(&wk, scope_group, scope);
		obj_array_push(&wk, wk.vm.default_scope_stack, scope_group);
		obj_dict_foreach(&wk, original_scope, &scope, reassign_default_var);
		wk.vm.scope_stack = az_scope_stack_dup(&wk, wk.vm.default_scope_stack);
	}

	wk.vm.behavior.assign_variable = az_assign_wrapper;
	wk.vm.behavior.unassign_variable = az_unassign;
	wk.vm.behavior.push_local_scope = az_push_local_scope;
	wk.vm.behavior.pop_local_scope = az_pop_local_scope;
	wk.vm.behavior.get_variable = az_lookup_wrapper;
	wk.vm.behavior.scope_stack_dup = az_scope_stack_dup;
	wk.vm.behavior.eval_project_file = az_eval_project_file;
	wk.vm.behavior.native_func_dispatch = az_native_func_dispatch;
	wk.vm.behavior.pop_args = az_pop_args;
	wk.vm.behavior.func_lookup = az_func_lookup;
	wk.vm.in_analyzer = true;

	analyzer.unpatched_ops = wk.vm.ops;

	wk.vm.ops.ops[op_az_branch] = az_op_az_branch;
	wk.vm.ops.ops[op_az_merge] = az_op_az_merge;
	wk.vm.ops.ops[op_jmp_if_disabler] = az_op_jmp_if_disabler;
	wk.vm.ops.ops[op_jmp_if_disabler_keep] = az_op_jmp_if_disabler_keep;
	wk.vm.ops.ops[op_jmp_if_false] = az_op_jmp_if_false;
	wk.vm.ops.ops[op_jmp_if_true] = az_op_jmp_if_true;
	wk.vm.ops.ops[op_constant_func] = az_op_constant_func;
	wk.vm.ops.ops[op_return] = az_op_return;
	wk.vm.ops.ops[op_return_end] = az_op_return_end;
	wk.vm.ops.ops[op_call] = az_op_call;
	wk.vm.ops.ops[op_add_store] = az_op_add_store;

	error_diagnostic_store_init(&wk);

	arr_init(&az_entrypoint_stack, 32, sizeof(struct az_file_entrypoint));
	arr_init(&az_entrypoint_stacks, 32, sizeof(struct az_file_entrypoint));

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
	} else {
		uint32_t project_id;
		res = eval_project(&wk, NULL, wk.source_root, wk.build_root, &project_id);
	}

	if (az_diagnostic_enabled(az_diagnostic_unused_variable)) {
		uint32_t i;
		for (i = 0; i < assignments.len; ++i) {
			struct assignment *a = bucket_arr_get(&assignments, i);
			if (!a->default_var && !a->accessed && *a->name != '_') {
				const char *msg = get_cstr(&wk, make_strf(&wk, "unused variable %s", a->name));
				enum log_level lvl = log_warn;
				error_diagnostic_store_push(a->src_idx, a->location, lvl, msg);

				if (analyzer.opts->subdir_error && a->ep_stack_len) {
					mark_az_entrypoint_as_containing_diagnostic(a->ep_stacks_i, lvl);
				}
			}
		}
	}

	if (az_diagnostic_enabled(az_diagnostic_dead_code)) {
		uint32_t i, *ip;
		for (i = 0; i < analyzer.branch_map.keys.len; ++i) {
			ip = arr_get(&analyzer.branch_map.keys, i);
			const union branch_map *map = (union branch_map *)hash_get(&analyzer.branch_map, ip);
			if (!map->data.impure) {
				if (!map->data.taken && map->data.not_taken) {
					switch (map->data.type) {
					case branch_map_type_normal:
						vm_warning_at(&wk, *ip, "branch never taken");
						break;
					case branch_map_type_ternary:
						vm_warning_at(&wk, *ip, "true branch never evaluated");
						break;
					}
				} else if (map->data.taken && !map->data.not_taken) {
					switch (map->data.type) {
					case branch_map_type_normal:
						vm_warning_at(&wk, *ip, "branch always taken");
						break;
					case branch_map_type_ternary:
						vm_warning_at(&wk, *ip, "false branch never evaluated");
						break;
					}
				}
			}
		}
	}

	uint32_t i;
	for (i = 0; i < az_entrypoint_stacks.len;) {
		uint32_t j;
		struct az_file_entrypoint *ep_stack = arr_get(&az_entrypoint_stacks, i);
		assert(ep_stack->is_root);

		uint32_t len = 1;
		for (j = 1; j + i < az_entrypoint_stacks.len && !ep_stack[j].is_root; ++j) {
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
	arr_destroy(&az_entrypoint_stack);
	arr_destroy(&az_entrypoint_stacks);
	workspace_destroy(&wk);
	return res;
}
