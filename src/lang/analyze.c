/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <sys/sysctl.h>

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "external/tinyjson.h"
#include "lang/analyze.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "memmem.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "tracy.h"

static struct {
	const struct az_opts *opts;
	struct obj_func *fp;
	uint32_t impure_loop_depth;
	bool error;

	struct vm_ops unpatched_ops;
	struct obj_typeinfo az_injected_native_func_return;
	struct hash branch_map;
	struct arr visited_ops;
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

struct obj_tainted_by_typeinfo_opts {
	bool allow_tainted_dict_values;
};

static bool
obj_tainted_by_typeinfo(struct workspace *wk, obj o, struct obj_tainted_by_typeinfo_opts *opts)
{
	if (!o) {
		return true;
	}

	switch (get_obj_type(wk, o)) {
	case obj_typeinfo: {
		return true;
	}
	case obj_array: {
		obj v;
		obj_array_for(wk, o, v) {
			if (obj_tainted_by_typeinfo(wk, v, opts)) {
				return true;
			}
		}

		break;
	}
	case obj_dict: {
		const bool disallow_tainted_values = opts && !opts->allow_tainted_dict_values;

		obj k, v;
		obj_dict_for(wk, o, k, v) {
			if (obj_tainted_by_typeinfo(wk, k, 0)) {
				return true;
			} else if (disallow_tainted_values && obj_tainted_by_typeinfo(wk, v, 0)) {
				return true;
			}
		}
		break;
	}
	default: break;
	}

	return false;
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

obj
make_typeinfo(struct workspace *wk, type_tag t)
{
	obj res;
	res = make_obj(wk, obj_typeinfo);
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
					LO("      %o: %s %o\n", k, assign->accessed ? "a" : "_", assign->o);
				}
			} else {
				obj_array_for(wk, scope_group, scope) {
					struct assignment *assign = bucket_arr_get(&assignments, v);
					L("    scope group:");
					obj_dict_for(wk, scope, k, v) {
						LO("      %o: %s %o\n", k, assign->accessed ? "a" : "_", assign->o);
					}
				}
			}

			++i;
		}
	}
}

// example scope_stack: [[{a: 1}, [{b: 2}, {b: 3}], [{c: 4}]]]
// example local_scope: [{a: 1}, [{b: 2}, {b: 3}], [{c: 4}]]
static bool
assign_lookup_with_scope(struct workspace *wk, const char *name, obj *scope, obj *res)
{
	TracyCZoneAutoS;

	bool found = false;
	obj local_scope;
	obj_array_for(wk, wk->vm.scope_stack, local_scope) {
		obj base;
		base = obj_array_index(wk, local_scope, 0);

		uint32_t local_scope_len = get_obj_array(wk, local_scope)->len;
		if (local_scope_len > 1) {
			int32_t i;
			// example: {a: 1},           -- skip
			// example: [{b: 2}, {b: 3}], -- take last
			// example: [{c: 4}]          -- take last
			for (i = local_scope_len - 1; i >= 1; --i) {
				obj scope_group;
				scope_group = obj_array_index(wk, local_scope, i);
				*scope = obj_array_get_tail(wk, scope_group);

				if (obj_dict_index_str(wk, *scope, name, res)) {
					found = true;
					break;
				}
			}
		}

		if (!found) {
			if (obj_dict_index_str(wk, base, name, res)) {
				*scope = base;
				found = true;
			}
		}
	}

	TracyCZoneAutoE;
	return found;
}

static struct assignment *
assign_lookup(struct workspace *wk, const char *name)
{
	obj res, _;
	if (assign_lookup_with_scope(wk, name, &_, &res)) {
		return bucket_arr_get(&assignments, res);
	}

	return 0;
}

static obj
assign_lookup_scope(struct workspace *wk, const char *name)
{
	obj _, scope;
	if (assign_lookup_with_scope(wk, name, &scope, &_)) {
		return scope;
	}

	return 0;
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
	scope_group = make_obj(wk, obj_array);
	obj_array_push(wk, local_scope, scope_group);
}

static void
push_scope_group_scope(struct workspace *wk)
{
	obj local_scope = obj_array_get_tail(wk, wk->vm.scope_stack);
	obj scope_group = obj_array_get_tail(wk, local_scope);
	obj scope;

	scope = make_obj(wk, obj_dict);
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

static void
pop_scope_group(struct workspace *wk)
{
	obj local_scope = obj_array_get_tail(wk, wk->vm.scope_stack);

	if (get_obj_array(wk, local_scope)->len == 1) {
		return;
	}

	obj scope_group = obj_array_pop(wk, local_scope);

	obj merged = obj_array_index(wk, scope_group, 0);

	{ // First, merge all scopes other than the root scope into `merged`
		bool first = true;
		obj scope;
		obj_array_for(wk, scope_group, scope) {
			if (first) {
				first = false;
				continue;
			}

			obj k, aid;
			obj_dict_for(wk, scope, k, aid) {
				obj bid;
				struct assignment *b, *a = bucket_arr_get(&assignments, aid);
				if (obj_dict_index(wk, merged, k, &bid)) {
					b = bucket_arr_get(&assignments, bid);
					merge_objects(wk, b, a);
				} else {
					obj_dict_set(wk, merged, k, aid);
				}
			}
		}
	}

	{ // Now, merge `merged` into base
		obj k, aid;
		obj_dict_for(wk, merged, k, aid) {
			(void)k;
			struct assignment *a = bucket_arr_get(&assignments, aid), *b;
			if ((b = assign_lookup(wk, a->name))) {
				merge_objects(wk, b, a);
			} else {
				if (false) {
					// This code makes all assignments within scope groups become
					// impure on pop.  I'm not sure why I added this because it messes up code like this:
					//
					// if use_i18n
					//   i18n = import('i18n')
					// endif
					//
					// With the below block enabled i18n becomes impure afterward
					// so we can't typecheck any of its methods.
					type_tag type = get_obj_type(wk, a->o);
					if (type != obj_typeinfo) {
						a->o = make_typeinfo(wk, obj_type_to_tc_type(type));
					}
				}

				b = scope_assign(wk, a->name, a->o, 0, assign_local);
				b->accessed = a->accessed;
				b->location = a->location;
				b->src_idx = a->src_idx;

				a->accessed = true;
			}
		}
	}
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
	uint32_t branches = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

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

	push_scope_group(wk);

	/* L("---> branching, merging @ %03x", cur_branch_group.merge_point); */

	bool pure = true;
	obj branch, expr_result = 0;
	obj_array_for(wk, branches, branch) {
		/* L("--> branch"); */
		cur_branch_group.branch = (union az_branch_element){ .i64 = get_obj_number(wk, branch) }.data;
		cur_branch_group.result = (struct branch_map_data){ 0 };
		wk->vm.ip = cur_branch_group.branch.ip;
		vm_push_call_stack_frame(wk, &(struct call_frame){ .type = call_frame_type_eval });
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

	/* L("<--- all branches merged %03x <---", cur_branch_group.merge_point); */

	pop_scope_group(wk);

	stack_pop(&wk->stack, cur_branch_group);

	if (expr_result && !pure) {
		object_stack_push(wk, expr_result);
	}
}

static void
az_op_az_merge(struct workspace *wk)
{
	/* L("<--- joining branch %03x, %03x", cur_branch_group.merge_point, wk->vm.ip - 1); */

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

#if 0
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
#endif

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
	scope_group = make_obj(wk, obj_array);
	obj scope;
	scope = make_obj(wk, obj_dict);
	obj_array_push(wk, scope_group, scope);
	obj_array_push(wk, wk->vm.scope_stack, scope_group);
}

static void
az_pop_local_scope(struct workspace *wk)
{
	obj scope_group = obj_array_pop(wk, wk->vm.scope_stack);
	assert(get_obj_array(wk, scope_group)->len == 1);
}

static obj
az_scope_stack_dup(struct workspace *wk, obj scope_stack)
{
	obj dup, local_scope, scope_group, scope;
	obj local_scope_dup, scope_group_dup, scope_dup;
	dup = make_obj(wk, obj_array);

	obj_array_for(wk, scope_stack, local_scope) {
		local_scope_dup = make_obj(wk, obj_array);

		uint32_t i = 0;
		obj_array_for(wk, local_scope, scope_group) {
			if (i == 0) {
				obj_dict_dup(wk, scope_group, &scope_dup);
				obj_array_push(wk, local_scope_dup, scope_dup);
			} else {
				scope_group_dup = make_obj(wk, obj_array);

				obj_array_for(wk, scope_group, scope) {
					obj_dict_dup(wk, scope, &scope_dup);
					obj_array_push(wk, scope_group_dup, scope_dup);
				}

				obj_array_push(wk, local_scope_dup, scope_group_dup);
			}

			++i;
		}

		obj_array_push(wk, dup, local_scope_dup);
	}

	return dup;
}

static bool
az_eval_project_file(struct workspace *wk,
	const char *path,
	enum build_language lang,
	enum eval_project_file_flags flags)
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

		enum eval_mode eval_mode = 0;
		if (flags & eval_project_file_flag_first) {
			eval_mode |= eval_mode_first;
		}

		if (!eval(wk, &src, lang, eval_mode, &res)) {
			goto ret;
		}

		ret = true;
ret:
		return ret;
	}

	if (analyzer.opts->analyze_project_call_only) {
		flags |= eval_project_file_flag_return_after_project;
	}

	return eval_project_file(wk, newpath, lang, flags);
}

static void
az_execute_loop(struct workspace *wk)
{
	arr_grow_to(&analyzer.visited_ops, wk->vm.code.len);

	uint32_t cip;
	while (wk->vm.run) {
		if (log_should_print(log_debug)) {
			/* LL("%-50s", vm_dis_inst(wk, wk->vm.code.e, wk->vm.ip)); */
			/* object_stack_print(wk, &wk->vm.stack); */
		}

		cip = wk->vm.ip;
		++wk->vm.ip;
		analyzer.visited_ops.e[cip] = 1;
		wk->vm.ops.ops[wk->vm.code.e[cip]](wk);
	}
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

	// If type includes dict, then we could potentially respond to anything
	if ((t & tc_dict & ~TYPE_TAG_MASK)) {
		return false;
	}

	// If type includes module, then we can't be sure what methods it might
	// respond to
	if ((t & tc_module & ~TYPE_TAG_MASK)) {
		res_t.type = tc_any;
		goto return_injected_native_func;
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
		goto return_injected_native_func;
	} else {
		// Single match found, return it
		*idx = lookup_res.idx;
	}

	return true;

return_injected_native_func:
	analyzer.az_injected_native_func_return = res_t;
	*idx = az_func_impl_group.off;
	*func = 0;
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

	if (self
		&& obj_tainted_by_typeinfo(
			wk, self, &(struct obj_tainted_by_typeinfo_opts){ .allow_tainted_dict_values = true })) {
		pure = false;
	}

	if (!self) {
		if (strcmp(native_funcs[func_idx].name, "subdir") == 0
			|| strcmp(native_funcs[func_idx].name, "subproject") == 0
			|| strcmp(native_funcs[func_idx].name, "dependency") == 0) {
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

/*
 */

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
			an[i].node = wk->vm.ip - 1;
		}
		an[i].type = ARG_TYPE_NULL;

		for (i = 0; i < capture->func->nkwargs; ++i) {
			akw[i].key = capture->func->akw[i].key;
			akw[i].val = make_typeinfo(wk, flatten_type(wk, capture->func->akw[i].type));
			akw[i].node = wk->vm.ip - 1;
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

/******************************************************************************
 * eval trace helpers
 ******************************************************************************/

struct eval_trace_print_ctx {
	uint32_t indent, len, i;
	uint64_t bars;
};

static uint32_t
eval_trace_arr_len(struct workspace *wk, obj arr)
{
	uint32_t cnt = 0;
	obj v;
	obj_array_for(wk, arr, v) {
		if (get_obj_type(wk, v) != obj_array) {
			++cnt;
		}
	}

	return cnt;
}

static void
eval_trace_print_level(struct workspace *wk, struct eval_trace_print_ctx *ctx, obj v)
{
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

		obj sub_v;
		obj_array_for(wk, v, sub_v) {
			eval_trace_print_level(wk, &subctx, sub_v);
		}
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

		TSTR(rel);
		if (path_is_absolute(get_cstr(wk, v))) {
			TSTR(cwd);
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
}

void
eval_trace_print(struct workspace *wk, obj trace)
{
	struct eval_trace_print_ctx ctx = {
		.indent = 1,
		.len = eval_trace_arr_len(wk, trace),
	};
	obj v;
	obj_array_for(wk, trace, v) {
		eval_trace_print_level(wk, &ctx, v);
	}
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
 * diagnostic helpers
 ******************************************************************************/

static void
az_warn_dead_code(struct workspace *wk,
	uint32_t src_idx,
	struct source_location *start_loc,
	struct source_location *end_loc)
{
	if (src_idx == UINT32_MAX) {
		return;
	}

	// TOOD: how can this occur?
	if (end_loc->off < start_loc->off) {
		return;
	}

	struct source_location merged
		= { .off = start_loc->off, .len = (end_loc->off - start_loc->off) + end_loc->len };
	struct source *src = arr_get(&wk->vm.src, src_idx);

	error_message(src, merged, log_warn, 0, "dead code");
}

/******************************************************************************
 * entrypoint
 ******************************************************************************/

bool
do_analyze_internal(struct workspace *wk, struct az_opts *opts)
{
	bool res = false;
	analyzer.opts = opts;
	workspace_init_bare(wk);

	bucket_arr_init(&assignments, 512, sizeof(struct assignment));
	hash_init(&analyzer.branch_map, 1024, sizeof(uint32_t));
	arr_init_flags(&analyzer.visited_ops, 1024, 1, arr_flag_zero_memory);

	{ /* re-initialize the default scope */
		obj original_scope, scope_group, scope;
		original_scope = obj_array_index(wk, wk->vm.default_scope_stack, 0);
		wk->vm.default_scope_stack = make_obj(wk, obj_array);
		scope_group = make_obj(wk, obj_array);
		scope = make_obj(wk, obj_dict);
		obj_array_push(wk, scope_group, scope);
		obj_array_push(wk, wk->vm.default_scope_stack, scope_group);
		obj k, v;
		obj_dict_for(wk, original_scope, k, v) {
			obj aid = push_assignment(wk, get_cstr(wk, k), v, 0);

			struct assignment *a = bucket_arr_get(&assignments, aid);
			a->default_var = true;

			obj_dict_set(wk, scope, k, aid);
		}
		wk->vm.scope_stack = az_scope_stack_dup(wk, wk->vm.default_scope_stack);
	}

	wk->vm.behavior.assign_variable = az_assign_wrapper;
	wk->vm.behavior.unassign_variable = az_unassign;
	wk->vm.behavior.push_local_scope = az_push_local_scope;
	wk->vm.behavior.pop_local_scope = az_pop_local_scope;
	wk->vm.behavior.get_variable = az_lookup_wrapper;
	wk->vm.behavior.scope_stack_dup = az_scope_stack_dup;
	wk->vm.behavior.eval_project_file = az_eval_project_file;
	wk->vm.behavior.native_func_dispatch = az_native_func_dispatch;
	wk->vm.behavior.pop_args = az_pop_args;
	wk->vm.behavior.func_lookup = az_func_lookup;
	wk->vm.behavior.execute_loop = az_execute_loop;
	wk->vm.in_analyzer = true;

	analyzer.unpatched_ops = wk->vm.ops;

	wk->vm.ops.ops[op_az_branch] = az_op_az_branch;
	wk->vm.ops.ops[op_az_merge] = az_op_az_merge;
	wk->vm.ops.ops[op_jmp_if_false] = az_op_jmp_if_false;
	wk->vm.ops.ops[op_jmp_if_true] = az_op_jmp_if_true;
	wk->vm.ops.ops[op_constant_func] = az_op_constant_func;
	wk->vm.ops.ops[op_return] = az_op_return;
	wk->vm.ops.ops[op_return_end] = az_op_return_end;
	wk->vm.ops.ops[op_call] = az_op_call;
	/* wk->vm.ops.ops[op_add_store] = az_op_add_store; */

	error_diagnostic_store_init(wk);

	arr_init(&az_entrypoint_stack, 32, sizeof(struct az_file_entrypoint));
	arr_init(&az_entrypoint_stacks, 32, sizeof(struct az_file_entrypoint));

	if (analyzer.opts->eval_trace) {
		wk->vm.dbg_state.eval_trace = make_obj(wk, obj_array);
	}

	if (analyzer.opts->file_override) {
		const char *root = determine_project_root(wk, analyzer.opts->file_override);
		if (root) {
			TSTR(cwd);
			path_copy_cwd(wk, &cwd);

			if (strcmp(cwd.buf, root) != 0) {
				path_chdir(root);
				wk->source_root = root;
			}
		}
	}

	if (opts->internal_file) {
		struct assignment *a = scope_assign(wk, "argv", make_typeinfo(wk, tc_array), 0, assign_local);
		a->default_var = true;

		wk->vm.lang_mode = language_extended;

		struct source src;
		if (!fs_read_entire_file(opts->internal_file, &src)) {
			res = false;
		} else {
			obj _v;
			res = eval(wk, &src, build_language_meson, 0, &_v);
		}
	} else {
		uint32_t project_id;
		workspace_init_runtime(wk);
		workspace_init_startup_files(wk);

		{
			obj wrap_mode;
			get_option(wk, 0, &STR("wrap_mode"), &wrap_mode);
			set_option(
				wk, wrap_mode, make_str(wk, "forcefallback"), option_value_source_commandline, false);
		}

		res = eval_project(wk, NULL, wk->source_root, wk->build_root, &project_id);
	}

	if (az_diagnostic_enabled(az_diagnostic_unused_variable)) {
		uint32_t i;
		for (i = 0; i < assignments.len; ++i) {
			struct assignment *a = bucket_arr_get(&assignments, i);
			if (!a->default_var && !a->accessed && *a->name != '_') {
				const char *msg = get_cstr(wk, make_strf(wk, "unused variable %s", a->name));
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
						vm_warning_at(wk, *ip, "branch never taken");
						break;
					case branch_map_type_ternary:
						vm_warning_at(wk, *ip, "true branch never evaluated");
						break;
					}
				} else if (map->data.taken && !map->data.not_taken) {
					switch (map->data.type) {
					case branch_map_type_normal:
						vm_warning_at(wk, *ip, "branch always taken");
						break;
					case branch_map_type_ternary:
						vm_warning_at(wk, *ip, "false branch never evaluated");
						break;
					}
				}
			}
		}

		// If this isn't true then we probaly failed to parse a file
		if (wk->vm.code.len == analyzer.visited_ops.len) {
			bool in_dead_code = false;
			uint32_t start_src_idx, src_idx;
			struct source_location start_loc, loc;

			for (i = 5; i < wk->vm.code.len; i += OP_WIDTH(wk->vm.code.e[i])) {
				if (!analyzer.visited_ops.e[i]) {
					vm_lookup_inst_location_src_idx(&wk->vm, i, &loc, &src_idx);

					if (!in_dead_code) {
						in_dead_code = true;
						start_loc = loc;
						start_src_idx = src_idx;
					}

					assert(src_idx == start_src_idx);
				} else if (in_dead_code) {
					az_warn_dead_code(wk, src_idx, &start_loc, &loc);
					in_dead_code = false;
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
		eval_trace_print(wk, wk->vm.dbg_state.eval_trace);
	} else if (analyzer.opts->get_definition_for) {
		bool found = false;
		uint32_t i;
		for (i = 0; i < assignments.len; ++i) {
			struct assignment *a = bucket_arr_get(&assignments, i);
			if (strcmp(a->name, analyzer.opts->get_definition_for) == 0) {
				struct source *src = arr_get(&wk->vm.src, a->src_idx);
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
	arr_destroy(&analyzer.visited_ops);
	return res;
}

bool
do_analyze(struct az_opts *opts)
{
	struct workspace wk;
	bool res = do_analyze_internal(&wk, opts);
	workspace_destroy(&wk);
	return res;
}

bool
analyze_project_call(struct workspace *wk)
{
	struct az_opts opts = {
		.replay_opts = error_diagnostic_store_replay_errors_only,
		.analyze_project_call_only = true,
	};

	return do_analyze_internal(wk, &opts);
}

/*******************************************************************************
 * LSP
 ******************************************************************************/

struct az_server {
	struct sbuf *in_buf;
	int in;
	FILE *out;
};

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "version.h"

// Returns true if the current process is being debugged (either
// running under the debugger or has a debugger attached post facto).
static bool AmIBeingDebugged(void)
{
    int                 junk;
    int                 mib[4];
    struct kinfo_proc   info;
    size_t              size;

    // Initialize the flags so that, if sysctl fails for some bizarre
    // reason, we get a predictable result.

    info.kp_proc.p_flag = 0;

    // Initialize mib, which tells sysctl the info we want, in this case
    // we're looking for information about a specific process ID.

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    // Call sysctl.

    size = sizeof(info);
    junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
    assert(junk == 0);

    // We're being debugged if the P_TRACED flag is set.

    return ( (info.kp_proc.p_flag & P_TRACED) != 0 );
}

static bool
az_server_read_bytes(struct workspace *wk, struct az_server *srv)
{
	struct sbuf *buf = srv->in_buf;

	{
		while (true) {
			struct pollfd fds = {
				.fd = srv->in,
				.events = POLLIN,
			};

			if (poll(&fds, 1, 250) == -1) {
				LOG_E("poll: %s", strerror(errno));
				return false;
			}

			if (fds.revents & POLLIN) {
				break;
			}
		}
	}

	if (buf->cap - buf->len < 16) {
		sbuf_grow(wk, buf, 1024);
	}

	ssize_t n = read(srv->in, buf->buf + buf->len, buf->cap - buf->len);
	if (n == 0) {
		// EOF
		return false;
	} else if (n < 0) {
		LOG_E("read: %s", strerror(errno));
		return false;
	}

	buf->len += n;

	fflush(_log_file());

	return true;
}

static void
az_server_buf_shift(struct az_server *srv, uint32_t amnt)
{
	struct sbuf *buf = srv->in_buf;

	char *start = buf->buf + amnt;
	buf->len = buf->len - (start - buf->buf);
	memmove(buf->buf, start, buf->len);
}

static bool
az_server_read(struct workspace *wk, struct az_server *srv, obj *msg)
{
	int64_t content_length = 0;
	struct sbuf *buf = srv->in_buf;

	{
		char *end;
		while (!(end = memmem(buf->buf, buf->len, "\r\n\r\n", 4))) {
			if (!az_server_read_bytes(wk, srv)) {
				LOG_E("Failed to read entire header");
				return false;
			}
		}

		*(end + 2) = 0;

		const char *hdr = buf->buf;
		char *hdr_end;
		while ((hdr_end = strstr(hdr, "\r\n"))) {
			*hdr_end = 0;
			char *val;
			if ((val = strchr(buf->buf, ':'))) {
				*val = 0;
				val += 2;

				if (strcmp(hdr, "Content-Length") == 0) {
					if (!str_to_i(&WKSTR(val), &content_length, false)) {
						LOG_E("Invalid value for Content-Length");
					}
				} else if (strcmp(hdr, "Content-Type") == 0) {
					// Ignore
				} else {
					LOG_E("Unknown header: %s", hdr);
				}
			} else {
				LOG_E("Header missing ':': %s", hdr);
			}

			if (hdr_end == end) {
				break;
			}
		}

		if (!content_length) {
			LOG_E("Missing Content-Length header.");
			return false;
		}

		az_server_buf_shift(srv, (hdr_end - buf->buf) + 4);
	}

	{
		while (buf->len < content_length) {
			if (!az_server_read_bytes(wk, srv)) {
				LOG_E("Failed to read entire message");
				return false;
			}
		}

		char end = buf->buf[content_length];
		buf->buf[content_length] = 0;
		if (!muon_json_to_dict(wk, buf->buf, msg)) {
			LOG_E("failed to parse json: '%.*s'", buf->len, buf->buf);
			return false;
		}
		buf->buf[content_length] = end;

		az_server_buf_shift(srv, content_length);
	}

	return true;
}

static bool
az_server_write(struct workspace *wk, struct az_server *srv, obj msg)
{
	SBUF(buf);
	obj_to_json(wk, msg, &buf);

	fprintf(srv->out, "Content-Length: %d\r\n\r\n%s", buf.len, buf.buf);
	fflush(srv->out);
	return true;
}

static const struct str *
obj_dict_index_as_str(struct workspace *wk, obj dict, const char *s)
{
	obj r;
	if (!obj_dict_index_str(wk, dict, s, &r)) {
		return 0;
	}

	return get_str(wk, r);
}

static int64_t
obj_dict_index_as_obj(struct workspace *wk, obj dict, const char *s)
{
	obj r;
	if (!obj_dict_index_str(wk, dict, s, &r)) {
		return 0;
	}

	return r;
}

static obj
az_server_handle(struct workspace *wk, obj msg)
{
	obj result = 0;

	const struct str *method = obj_dict_index_as_str(wk, msg, "method");

	if (str_eql(method, &WKSTR("initialize"))) {
		make_obj(wk, &result, obj_dict);
		obj capabilities;
		make_obj(wk, &capabilities, obj_dict);
		// Set textDocumentSync type to 4
		obj_dict_set(wk, capabilities, make_str(wk, "textDocumentSync"), make_number(wk, 1));
		obj_dict_set(wk, result, make_str(wk, "capabilities"), capabilities);

		obj server_info;
		make_obj(wk, &server_info, obj_dict);
		obj_dict_set(wk, server_info, make_str(wk, "name"), make_str(wk, "muon"));
		obj_dict_set(wk, server_info, make_str(wk, "version"), make_str(wk, muon_version.version));

		obj_dict_set(wk, result, make_str(wk, "serverInfo"), server_info);
	}

	if (result) {
		obj response;
		make_obj(wk, &response, obj_dict);
		obj_dict_set(wk, response, make_str(wk, "jsonrpc"), make_str(wk, "2.0"));
		obj_dict_set(wk, response, make_str(wk, "id"), obj_dict_index_as_obj(wk, msg, "id"));
		obj_dict_set(wk, response, make_str(wk, "result"), result);
		return response;
	} else {
		return 0;
	}
}

bool
analyze_server(struct az_opts *opts)
{
	bool loop = true;

	FILE *log_file = 0;
	if (!(log_file = fs_fopen("lsp.log", "wb"))) {
		return false;
	}
	log_set_file(log_file);

	/* LOG_I("muon lsp waiting for debugger..."); */
	/* while (!AmIBeingDebugged()) { */
	/* } */

	LOG_I("muon lsp listening...");

	struct az_server srv = { .in = STDIN_FILENO, .out = stdout };

	while (loop) {
		struct workspace wk = { 0 };
		workspace_init_bare(&wk);
		SBUF(in_buf);
		srv.in_buf = &in_buf;

		obj msg;
		if (!az_server_read(&wk, &srv, &msg)) {
			break;
		}

		obj_lprintf(&wk, "<<< %#o\n", msg);
		fflush(log_file);

		obj resp;
		if ((resp = az_server_handle(&wk, msg))) {
			obj_lprintf(&wk, ">>> %#o\n", msg);
			fflush(log_file);

			if (!az_server_write(&wk, &srv, resp)) {
				break;
			}
		}

		workspace_destroy(&wk);
	}
	LOG_I("muon lsp shutting down");

	if (log_file) {
		fs_fclose(log_file);
		log_set_file(stdout);
	}
	return true;
}
