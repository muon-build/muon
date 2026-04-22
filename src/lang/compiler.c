/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdarg.h>
#include <string.h>

#include "buf_size.h"
#include "lang/analyze.h"
#include "lang/compiler.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/parser.h"
#include "lang/typecheck.h"
#include "lang/vm.h"
#include "lang/workspace.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "tracy.h"

/******************************************************************************
 * compiler
 ******************************************************************************/

static void
push_location(struct workspace *wk, struct node *n)
{
	arr_push(wk->a, &wk->vm.locations,
		&(struct source_location_mapping){
			.ip = wk->vm.code.len,
			.loc = n->location,
			.src_idx = wk->vm.src.len - 1,
		});
}

typedef void((*vm_visit_nodes_cb)(struct workspace *wk, struct node *n));

static void
vm_visit_nodes(struct workspace *wk,
	struct node *n,
	const enum node_type privelaged[],
	uint32_t privelaged_len,
	vm_visit_nodes_cb cb)
{
	struct node *peek, *prev = 0;

	uint32_t stack_base = wk->vm.compiler_state.node_stack.len;

	while (wk->vm.compiler_state.node_stack.len > stack_base || n) {
		if (n) {
			bool is_privelaged = false;
			for (uint32_t i = 0; i < privelaged_len; ++i) {
				if (n->type == privelaged[i]) {
					is_privelaged = true;
					break;
				}
			}

			if (is_privelaged) {
				cb(wk, n);
				prev = n;
				n = 0;
			} else {
				arr_push(wk->a, &wk->vm.compiler_state.node_stack, &n);
				n = n->l;
			}
		} else {
			peek = *(struct node **)arr_peek(&wk->vm.compiler_state.node_stack, 1);
			if (peek->r && prev != peek->r) {
				n = peek->r;
			} else {
				push_location(wk, peek);
				cb(wk, peek);
				prev = *(struct node **)arr_pop(&wk->vm.compiler_state.node_stack);
			}
		}
	}
}

static void
push_code(struct workspace *wk, uint8_t b)
{
	arr_push(wk->a, &wk->vm.code, &b);
}

static void
push_constant_at(obj v, uint8_t *code)
{
	v = vm_constant_host_to_bc(v);
	code[0] = (v >> 16) & 0xff;
	code[1] = (v >> 8) & 0xff;
	code[2] = v & 0xff;
}

static void
push_constant(struct workspace *wk, obj v)
{
	v = vm_constant_host_to_bc(v);
	push_code(wk, (v >> 16) & 0xff);
	push_code(wk, (v >> 8) & 0xff);
	push_code(wk, v & 0xff);
}

static void
vm_comp_assign_global(struct workspace *wk, obj id, enum node_assign_flag flags)
{
	push_code(wk, op_constant);
	push_constant(wk, id);
	push_code(wk, (flags & node_assign_flag_add_store) ? op_add_store_g : op_store_g);
}

static void
vm_comp_diagnostic_v(struct workspace *wk, enum log_level lvl, struct node *n, const char *fmt, va_list args)
{
	struct source *src = arr_peek(&wk->vm.src, 1);
	error_messagev(wk, src, n->location, lvl, fmt, args);
	if (lvl == log_error) {
		wk->vm.compiler_state.err = true;
	}
}

static void vm_comp_error(struct workspace *wk, struct node *n, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);
static void
vm_comp_error(struct workspace *wk, struct node *n, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_comp_diagnostic_v(wk, log_error, n, fmt, args);
	va_end(args);
}

static void vm_comp_warning(struct workspace *wk, struct node *n, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);
static void
vm_comp_warning(struct workspace *wk, struct node *n, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_comp_diagnostic_v(wk, log_warn, n, fmt, args);
	va_end(args);
}

static void vm_comp_note(struct workspace *wk, struct node *n, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);
static void
vm_comp_note(struct workspace *wk, struct node *n, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_comp_diagnostic_v(wk, log_note, n, fmt, args);
	va_end(args);
}

static void
vm_comp_disable_in_script_mode(struct workspace *wk, struct node *n)
{
	const bool script_mode = wk->vm.compiler_state.mode & vm_compile_mode_language_extended;
	if (script_mode) {
		vm_comp_error(wk, n, "this function is not available in script mode");
	}
}

static void
vm_comp_assert_inline_func_args(struct workspace *wk,
	struct node *func_node,
	struct node *args_node,
	uint32_t args_min,
	uint32_t args_max,
	uint32_t kwargs)
{
	if (!(args_min <= args_node->data.len.args && args_node->data.len.args <= args_max
		    && args_node->data.len.kwargs == kwargs)) {
		vm_comp_error(wk,
			func_node,
			"invalid number of arguments for function, expected %d-%d args and %d kwargs",
			args_min,
			args_max,
			kwargs);
	}
}

enum vm_compile_block_flags {
	vm_compile_block_final_return = 1 << 0,
	vm_compile_block_expr = 1 << 1,
};

static void vm_compile_block(struct workspace *wk, struct node *n, enum vm_compile_block_flags flags);
static void vm_compile_expr(struct workspace *wk, struct node *n);

static bool
vm_comp_node_skip(enum node_type t)
{
	switch (t) {
	case node_type_id_lit:
	case node_type_args:
	case node_type_def_args:
	case node_type_kw:
	case node_type_list:
	case node_type_foreach_args:
	case node_type_stmt:
		return true;
	default: return false;
	}
}

static uint32_t
vm_comp_add_upvalue(struct workspace *wk, obj id, uint32_t depth, uint32_t slot, bool is_local)
{
	const struct compiler_call_frame *frame = arr_get(&wk->vm.compiler_state.call_stack, depth);

	uint32_t res_slot = 0;

	for (uint32_t i = frame->upvalues_base; i < wk->vm.compiler_state.upvalues.len; ++i) {
		struct upvalue_binding *u = arr_get(&wk->vm.compiler_state.upvalues, i);
		if (u->depth != depth) {
			continue;
		}

		if (slot == u->slot && is_local == u->is_local) {
			return res_slot;
		}

		++res_slot;
	}

	arr_push(wk->a, &wk->vm.compiler_state.upvalues, &(struct upvalue_binding){ depth, slot, id, is_local });
	return res_slot;
}

enum vm_comp_resolve_mode {
	vm_comp_resolve_mode_access,
	vm_comp_resolve_mode_assign,
};

static int32_t
vm_comp_resolve_upvalue_depth(struct workspace *wk, uint32_t depth, obj id)
{
	const struct compiler_call_frame *frame = arr_get(&wk->vm.compiler_state.call_stack, depth);

	for (uint32_t i = frame->locals_base; i < wk->vm.compiler_state.locals.len; ++i) {
		struct local_binding *l = arr_get(&wk->vm.compiler_state.locals, i);
		if (l->depth != depth) {
			break;
		}
		if (obj_equal(wk, id, l->id)) {
			l->accessed = true;
			return vm_comp_add_upvalue(wk, id, depth, l->slot, true);
		}
	}

	if (depth == 0) {
		return -1;
	}

	int32_t slot = vm_comp_resolve_upvalue_depth(wk, depth - 1, id);
	if (slot != -1) {
		return vm_comp_add_upvalue(wk, id, depth, slot, false);
	}

	return -1;
}

static int32_t
vm_comp_current_depth(struct workspace *wk)
{
	return wk->vm.compiler_state.call_stack.len - 1;
}

static int32_t
vm_comp_resolve_upvalue(struct workspace *wk, obj id)
{
	int32_t depth = vm_comp_current_depth(wk);
	if (!depth) {
		return -1;
	}
	return vm_comp_resolve_upvalue_depth(wk, depth - 1, id);
}

static void
vm_comp_declare_local_unchecked(struct workspace *wk, struct node *n, obj id)
{
	const uint32_t depth = vm_comp_current_depth(wk);
	const struct compiler_call_frame *frame = arr_get(&wk->vm.compiler_state.call_stack, depth);
	struct local_binding l = {
		n,
		id,
		depth,
		wk->vm.compiler_state.locals.len - frame->locals_base,
	};
	arr_push(wk->a, &wk->vm.compiler_state.locals, &l);
}

static int32_t
vm_comp_resolve_local(struct workspace *wk, obj id, enum vm_comp_resolve_mode mode)
{
	const uint32_t depth = vm_comp_current_depth(wk);
	for (int32_t i = wk->vm.compiler_state.locals.len - 1; i >= 0; --i) {
		struct local_binding *l = arr_get(&wk->vm.compiler_state.locals, i);
		if (l->depth != depth) {
			return -1;
		} else if (!l->bound && (mode == vm_comp_resolve_mode_access)) {
			continue;
		}

		if (obj_equal(wk, id, l->id)) {
			if (mode == vm_comp_resolve_mode_assign) {
				l->bound = true;
			} else if (mode == vm_comp_resolve_mode_access) {
				l->accessed = true;
			}
			return l->slot;
		}
	}
	return -1;
}

enum vm_comp_local_type {
	vm_comp_local_type_local,
	vm_comp_local_type_upvalue,
	vm_comp_local_type_constant,
};

struct vm_comp_local {
	int32_t slot;
	enum vm_comp_local_type type;
};

static struct vm_comp_local
vm_comp_resolve_id(struct workspace *wk, obj id, enum vm_comp_resolve_mode mode)
{
	struct vm_comp_local res = { -1 };
	if ((res.slot = vm_comp_resolve_local(wk, id, mode)) != -1) {
		res.type = vm_comp_local_type_local;
	} else if ((res.slot = vm_comp_resolve_upvalue(wk, id)) != -1) {
		res.type = vm_comp_local_type_upvalue;
	} else {
		obj var;
		if (obj_dict_index(wk, wk->vm.default_global_scope, id, &var)) {
			res.slot = var;
			res.type = vm_comp_local_type_constant;
		}
	}
	return res;
}

static bool
vm_comp_declare_local(struct workspace *wk, struct node *n, obj id)
{
	const uint32_t depth = vm_comp_current_depth(wk);
	for (int32_t i = wk->vm.compiler_state.locals.len - 1; i >= 0; --i) {
		struct local_binding *l = arr_get(&wk->vm.compiler_state.locals, i);
		if (l->depth != depth) {
			break;
		}
		if (obj_equal(wk, id, l->id)) {
			vm_comp_error(wk, n, "duplicate variable name %s", get_cstr(wk, id));
			vm_comp_note(wk, l->n, "already declared here");
			return false;
		}
	}

	vm_comp_declare_local_unchecked(wk, n, id);
	return true;
}

static void
vm_comp_assign_local(struct workspace *wk, struct node *n, obj id, enum node_assign_flag flags)
{
	struct vm_comp_local l = vm_comp_resolve_id(wk,
		id,
		(flags & node_assign_flag_add_store) ? vm_comp_resolve_mode_access : vm_comp_resolve_mode_assign);
	if (l.slot == -1) {
		if (flags & node_assign_flag_add_store) {
			vm_comp_error(wk, n, "undefined variable");
		} else {
			UNREACHABLE;
		}
	}

	switch (l.type) {
	case vm_comp_local_type_local: push_code(wk, (flags & node_assign_flag_add_store) ? op_add_store_l : op_store_l); break;
	case vm_comp_local_type_upvalue: push_code(wk, (flags & node_assign_flag_add_store) ? op_add_store_u : op_store_u); break;
	case vm_comp_local_type_constant: push_code(wk, op_constant); break;
	}
	push_constant(wk, l.slot);
}

static void
vm_comp_try_declare_block_local(struct workspace *wk, struct node *n, obj id)
{
	for (int32_t i = wk->vm.compiler_state.locals.len - 1; i >= 0; --i) {
		struct local_binding *l = arr_get(&wk->vm.compiler_state.locals, i);
		if (obj_equal(wk, id, l->id)) {
			return;
		}
	}

	vm_comp_declare_local_unchecked(wk, n, id);
	push_code(wk, op_constant);
	push_constant(wk, id);
}

static void
vm_comp_block_locals_visitor(struct workspace *wk, struct node *n)
{
	switch (n->type)
	{
	case node_type_assign: {
		if (!(n->data.type & node_assign_flag_member) && !(n->data.type & node_assign_flag_add_store)) {
			assert(n->l->type == node_type_id_lit);
			if (n->data.type & node_assign_flag_force_declaration) {
				vm_comp_declare_local(wk, n->l, n->l->data.str);
			} else {
				vm_comp_try_declare_block_local(wk, n->l, n->l->data.str);
			}
		}
		break;
	}
	case node_type_foreach: {
		struct node *ida = n->l->l->l, *idb = n->l->l->r;
		vm_comp_try_declare_block_local(wk, ida, ida->data.str);
		if (idb) {
			vm_comp_try_declare_block_local(wk, idb, idb->data.str);
		}
		break;
	}
	case node_type_func_def: {
		struct node *id = n->l->l->l;
		if (id) {
			vm_comp_try_declare_block_local(wk, id, id->data.str);
		}
		break;
	}
	default:
		break;
	}
}

static void
vm_comp_block_locals(struct workspace *wk, struct node *n)
{
	const enum node_type privelaged[] = { node_type_func_def };

	// All the local variables that need declarations for this function are
	// scanned here, and ops are created to fill out placeholders in the
	// stack.
	//
	// The reason we do this upfront is because the entire function body is
	// a single flat scope, yet things like iterators put objects on the
	// stack.
	//
	// Locals declared in this phase are have bound set to false which
	// means that the compiler will still error if you try to use a var
	// before it is assigned.
	while (n && n->l) {
		vm_visit_nodes(wk, n->l, privelaged, ARRAY_LEN(privelaged), vm_comp_block_locals_visitor);
		n = n->r;
	}
}

static void
vm_comp_push_call_frame(struct workspace *wk)
{
	stack_push(&wk->stack, wk->vm.compiler_state.loop_depth, 0);
	arr_push(wk->a,
		&wk->vm.compiler_state.call_stack,
		&(struct compiler_call_frame){
			.locals_base = wk->vm.compiler_state.locals.len,
			.upvalues_base = wk->vm.compiler_state.upvalues.len,
		});
}

static struct compiler_call_frame *
vm_comp_pop_call_frame(struct workspace *wk)
{
	struct compiler_call_frame *frame = arr_pop(&wk->vm.compiler_state.call_stack);
	const uint32_t depth = wk->vm.compiler_state.call_stack.len;
	frame->nupvalues = 0;

	for (int32_t i = wk->vm.compiler_state.upvalues.len - 1; i >= (int32_t)frame->upvalues_base; --i) {
		struct upvalue_binding *u = arr_get(&wk->vm.compiler_state.upvalues, i);
		if (u->depth == depth - 1) {
			++frame->nupvalues;
		}
	}

	frame->upvalues = ar_maken(wk->a, struct func_upvalue, frame->nupvalues);

	int32_t upvalue_i = frame->nupvalues - 1;
	for (int32_t i = wk->vm.compiler_state.upvalues.len - 1; i >= (int32_t)frame->upvalues_base; --i) {
		struct upvalue_binding *u = arr_get(&wk->vm.compiler_state.upvalues, i);
		if (u->depth == depth - 1) {
			// struct compiler_call_frame *parent_frame = arr_peek(&wk->vm.compiler_state.call_stack, 1);
			// struct local_binding *l
			// 	= arr_get(&wk->vm.compiler_state.locals, parent_frame->locals_base + u->slot);
			frame->upvalues[upvalue_i] = (struct func_upvalue){
				.slot = u->slot,
				.is_local = u->is_local,
				.id = u->id,
			};
			arr_del(&wk->vm.compiler_state.upvalues, i);
			--upvalue_i;
		}
	}

	uint32_t locals_debug_len = 0;
	frame->locals_debug = ar_maken(wk->a, obj, wk->vm.compiler_state.locals.len - frame->locals_base);
	for (uint32_t i = frame->locals_base; i < wk->vm.compiler_state.locals.len; ++i) {
		struct local_binding *l = arr_get(&wk->vm.compiler_state.locals, i);
		if (!l->accessed && wk->vm.in_analyzer && get_str(wk, l->id)->s[0] != '_') {
			vm_comp_warning(wk, l->n, "unused variable");
		}
		frame->locals_debug[locals_debug_len] = l->id;
		++locals_debug_len;
	}

	wk->vm.compiler_state.locals.len = frame->locals_base;
	stack_pop(&wk->stack, wk->vm.compiler_state.loop_depth);
	return frame;
}

static void
vm_comp_init_func(struct workspace *wk, struct obj_func *func, const struct compiler_call_frame *frame, uint32_t nargs, uint32_t nkwargs)
{
	func->nargs = nargs;
	func->nkwargs = nkwargs;
	func->an = ar_maken(wk->a, struct args_norm, func->nargs + 1);
	func->akw = ar_maken(wk->a, struct args_kw, func->nkwargs + 1);
	func->an[func->nargs].type = ARG_TYPE_NULL;
	func->akw[func->nkwargs].key = 0;
	func->nupvalues = frame->nupvalues;
	func->upvalues = frame->upvalues;
	func->locals_debug = frame->locals_debug;
	func->lang_mode = wk->vm.lang_mode;
}

static void
vm_comp_node(struct workspace *wk, struct node *n)
{
	if (vm_comp_node_skip(n->type)) {
		return;
	}

	/* L("compiling %s", node_to_s(wk, n)); */

	if (n->flags & node_flag_breakpoint) {
		push_code(wk, op_dbg_break);
	}

	switch (n->type) {
	case node_type_stringify: push_code(wk, op_stringify); break;
	case node_type_index: push_code(wk, op_index); break;
	case node_type_negate: push_code(wk, op_negate); break;
	case node_type_add: push_code(wk, op_add); break;
	case node_type_sub: push_code(wk, op_sub); break;
	case node_type_mul: push_code(wk, op_mul); break;
	case node_type_div: push_code(wk, op_div); break;
	case node_type_mod: push_code(wk, op_mod); break;
	case node_type_not: push_code(wk, op_not); break;
	case node_type_eq: push_code(wk, op_eq); break;
	case node_type_neq:
		push_code(wk, op_eq);
		push_code(wk, op_not);
		break;
	case node_type_in: push_code(wk, op_in); break;
	case node_type_not_in:
		push_code(wk, op_in);
		push_code(wk, op_not);
		break;
	case node_type_lt: push_code(wk, op_lt); break;
	case node_type_gt: push_code(wk, op_gt); break;
	case node_type_leq:
		push_code(wk, op_gt);
		push_code(wk, op_not);
		break;
	case node_type_geq:
		push_code(wk, op_lt);
		push_code(wk, op_not);
		break;
	case node_type_id: {
		if (wk->vm.compiler_state.mode & vm_compile_mode_locals) {
			struct vm_comp_local l = vm_comp_resolve_id(wk, n->data.str, vm_comp_resolve_mode_access);
			if (l.slot == -1) {
				vm_comp_error(wk, n, "undefined variable");
			} else {
				switch (l.type) {
				case vm_comp_local_type_local: push_code(wk, op_load_l); break;
				case vm_comp_local_type_upvalue: push_code(wk, op_load_u); break;
				case vm_comp_local_type_constant: push_code(wk, op_constant); break;
				}
				push_constant(wk, l.slot);
			}
		} else {
			push_code(wk, op_constant);
			push_constant(wk, n->data.str);
			push_code(wk, op_load_g);
		}
		break;
	}
	case node_type_maybe_id:
		push_code(wk, op_constant);
		push_constant(wk, n->data.str);
		push_code(wk, op_dup);
		push_code(wk, op_try_load_g);
		break;
	case node_type_number:
		push_code(wk, op_constant);
		obj o;
		o = make_obj(wk, obj_number);
		set_obj_number(wk, o, n->data.num);
		push_constant(wk, o);
		break;
	case node_type_bool:
		push_code(wk, op_constant);
		push_constant(wk, n->data.num ? obj_bool_true : obj_bool_false);
		break;
	case node_type_null:
		push_code(wk, op_constant);
		push_constant(wk, 0);
		break;
	case node_type_string:
		push_code(wk, op_constant);
		push_constant(wk, n->data.str);
		break;
	case node_type_array:
		push_code(wk, op_constant_list);
		push_constant(wk, n->data.len.args);
		break;
	case node_type_dict:
		push_code(wk, op_constant_dict);
		push_constant(wk, n->data.len.kwargs);
		break;
	case node_type_assign: {
		const enum node_assign_flag flags = n->data.type;
		if (flags & node_assign_flag_member) {
			push_code(wk, (flags & node_assign_flag_add_store) ? op_add_store_m : op_store_m);
		} else if (wk->vm.compiler_state.mode & vm_compile_mode_locals) {
			assert(n->l->type == node_type_id_lit);
			vm_comp_assign_local(wk, n->l, n->l->data.str, flags);
		} else {
			assert(n->l->type == node_type_id_lit);
			vm_comp_assign_global(wk, n->l->data.str, flags);
		}
		break;
	}
	case node_type_member: {
		push_code(wk, op_member);
		push_constant(wk, n->r->data.str);
		break;
	}
	case node_type_call: {
		bool known = false;
		const struct str *name = 0;
		uint32_t idx;

		if (n->r->type == node_type_id_lit) {
			name = get_str(wk, n->r->data.str);

			if (str_eql(name, &STR("subdir_done"))) {
				vm_comp_disable_in_script_mode(wk, n);

				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 0, 0, 0);

				push_code(wk, op_constant);
				push_constant(wk, 0);

				push_code(wk, op_return);

				break;
			} else if (str_eql(name, &STR("set_variable"))) {
				vm_comp_disable_in_script_mode(wk, n);

				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 2, 2, 0);

				push_code(wk, op_swap);
				push_code(wk, op_store_g);
				break;
			} else if (str_eql(name, &STR("get_variable"))) {
				vm_comp_disable_in_script_mode(wk, n);

				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 1, 2, 0);

				if (n->l->data.len.args == 1) {
					push_code(wk, op_load_g);
				} else {
					push_code(wk, op_try_load_g);
				}
				break;
			} else if (str_eql(name, &STR("disabler"))) {
				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 0, 0, 0);

				push_code(wk, op_constant);
				push_constant(wk, obj_disabler);
				break;
			} else if (str_eql(name, &STR("is_disabler"))) {
				/* jmp_if_disabler >-,
				 * pop               |
				 * const false       |
				 * jmp >-------------|-,
				 * const true <------` |
				 *    <----------------`
				 */
				uint32_t true_jmp_tgt, false_jmp_tgt;

				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 1, 1, 0);

				push_code(wk, op_jmp_if_disabler);
				true_jmp_tgt = wk->vm.code.len;
				push_constant(wk, 0);

				push_code(wk, op_pop);
				push_code(wk, op_constant);
				push_constant(wk, obj_bool_false);
				push_code(wk, op_jmp);
				false_jmp_tgt = wk->vm.code.len;
				push_constant(wk, 0);

				push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, true_jmp_tgt));

				push_code(wk, op_constant);
				push_constant(wk, obj_bool_true);

				push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, false_jmp_tgt));
				break;
			}

			known = func_lookup(wk, 0, name->s, &idx, 0);
			if (!known) {
				n->r->type = node_type_id;
				push_location(wk, n->r);
				vm_comp_node(wk, n->r);
			}
		}

		push_location(wk, n);

		if (known) {
			push_code(wk, op_call_native);
			push_constant(wk, n->l->data.len.args);
			push_constant(wk, n->l->data.len.kwargs);
			push_constant(wk, idx);
		} else {
			push_code(wk, op_call);
			push_constant(wk, n->l->data.len.args);
			push_constant(wk, n->l->data.len.kwargs);
		}

		if (known && (wk->vm.compiler_state.mode & vm_compile_mode_return_after_project)
			&& str_eql(name, &STR("project"))) {
			push_code(wk, op_return_end);
		}

		break;
	}
	case node_type_return: {
		if (!n->l) {
			push_code(wk, op_constant);
			push_constant(wk, 0);
		}

		push_code(wk, op_return);
		break;
	}
	case node_type_foreach: {
		/* <iter expr>
		 *
		 * iterator
		 * iterator_next <-, >-,
		 *                 |   |
		 * constant ida    |   |
		 * store           |   |
		 * pop             |   |
		 *                 |   |
		 * if idb:         |   |
		 *   constant idb  |   |
		 *   store         |   |
		 *   pop           |   |
		 *                 |   |
		 * <block>         |   |
		 *   break:        |   |
		 *     jmp >-----------+
		 *   continue:     |   |
		 *     jmp >-------+   |
		 *                 |   |
		 * jmp >-----------`   |
		 *    <----------------`
		 */
		uint32_t break_jmp_patch_tgt, loop_body_start;
		struct node *ida = n->l->l->l, *idb = n->l->l->r;

		vm_compile_expr(wk, n->l->r);

		push_location(wk, n);

		push_code(wk, op_iterator);
		push_constant(wk, idb ? 2 : 1);

		uint32_t az_merge_point_tgt = 0;
		if (wk->vm.in_analyzer) {
			push_code(wk, op_az_branch);
			push_constant(wk, az_branch_type_loop);
			az_merge_point_tgt = wk->vm.code.len;
			push_constant(wk, 0);
			push_constant(wk, 1);
		}

		loop_body_start = wk->vm.code.len;

		push_code(wk, op_iterator_next);
		break_jmp_patch_tgt = wk->vm.code.len;
		push_constant(wk, 0);

		if (wk->vm.compiler_state.mode & vm_compile_mode_locals) {
			vm_comp_assign_local(wk, ida, ida->data.str, 0);
		} else {
			vm_comp_assign_global(wk, ida->data.str, 0);
		}
		push_code(wk, op_pop);

		if (idb) {
			if (wk->vm.compiler_state.mode & vm_compile_mode_locals) {
				vm_comp_assign_local(wk, idb, idb->data.str, 0);
			} else {
				vm_comp_assign_global(wk, idb->data.str, 0);
			}
			push_code(wk, op_pop);
		}

		uint32_t loop_jmp_stack_base = wk->vm.compiler_state.loop_jmp_stack.len;
		arr_push(wk->a, &wk->vm.compiler_state.loop_jmp_stack, &loop_body_start);

		++wk->vm.compiler_state.loop_depth;
		vm_compile_block(wk, n->r, 0);
		--wk->vm.compiler_state.loop_depth;

		push_code(wk, op_jmp);
		push_constant(wk, loop_body_start);
		push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, break_jmp_patch_tgt));

		arr_pop(&wk->vm.compiler_state.loop_jmp_stack);

		while (wk->vm.compiler_state.loop_jmp_stack.len > loop_jmp_stack_base) {
			break_jmp_patch_tgt = *(uint32_t *)arr_pop(&wk->vm.compiler_state.loop_jmp_stack);
			push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, break_jmp_patch_tgt));
		}

		if (wk->vm.in_analyzer) {
			push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, az_merge_point_tgt));
			push_code(wk, op_az_merge);
		}
		break;
	}
	case node_type_continue: {
		if (wk->vm.in_analyzer) {
			push_code(wk, op_az_noop);
			break;
		}

		push_code(wk, op_jmp);
		push_constant(wk, *(uint32_t *)arr_peek(&wk->vm.compiler_state.loop_jmp_stack, 1));
		break;
	}
	case node_type_break: {
		if (wk->vm.in_analyzer) {
			push_code(wk, op_az_noop);
			break;
		}

		push_code(wk, op_jmp);

		uint32_t top = *(uint32_t *)arr_pop(&wk->vm.compiler_state.loop_jmp_stack);
		arr_push(wk->a, &wk->vm.compiler_state.loop_jmp_stack, &wk->vm.code.len);
		arr_push(wk->a, &wk->vm.compiler_state.loop_jmp_stack, &top);

		push_constant(wk, 0);
		break;
	}
	case node_type_if: {
		/* <cond>
		 * jmp_if_disabler >-------,
		 * jmp_if_false >------,   |
		 * <block>             |   |
		 * jmp >---------------|---+
		 *   jmp_if_disabler <-` >-+
		 *   jmp_if_false >--,     |
		 *   <block>         |     |
		 *   jmp >-----------|-----+
		 *     <block> <-----`     |
		 * <-----------------------`
		 */
		uint32_t else_jmp = 0, end_jmp;
		uint32_t patch_tgts = 0;

		obj az_branches = 0;
		if (wk->vm.in_analyzer) {
			push_code(wk, op_az_branch);
			push_constant(wk, az_branch_type_normal);

			arr_push(wk->a, &wk->vm.compiler_state.if_jmp_stack, &wk->vm.code.len);
			++patch_tgts;
			push_constant(wk, 0);

			az_branches = make_obj(wk, obj_array);
			push_constant(wk, az_branches);
		}

		while (n) {
			if (wk->vm.in_analyzer) {
				obj_array_push(wk, az_branches, make_az_branch_element(wk, wk->vm.code.len, 0));
			}

			if (n->l->l) {
				vm_compile_expr(wk, n->l->l);
				push_code(wk, op_jmp_if_disabler);
				arr_push(wk->a, &wk->vm.compiler_state.if_jmp_stack, &wk->vm.code.len);
				++patch_tgts;
				push_constant(wk, 0);

				push_code(wk, op_jmp_if_false);
				else_jmp = wk->vm.code.len;
				push_constant(wk, 0);
			}

			vm_compile_block(wk, n->l->r, 0);

			push_code(wk, op_jmp);
			arr_push(wk->a, &wk->vm.compiler_state.if_jmp_stack, &wk->vm.code.len);
			++patch_tgts;
			push_constant(wk, 0);

			if (n->l->l) {
				push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, else_jmp));
			}

			n = n->r;
		}

		for (uint32_t i = 0; i < patch_tgts; ++i) {
			end_jmp = *(uint32_t *)arr_pop(&wk->vm.compiler_state.if_jmp_stack);
			push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, end_jmp));
		}

		if (wk->vm.in_analyzer) {
			push_code(wk, op_az_merge);
		}
		break;
	}
	case node_type_ternary: {
		/* <cond>
		 * jmp_if_disabler_keep >--,
		 * jmp_if_false >----------|-,
		 * <lhs>                   | |
		 * jmp >-------------------+ |
		 * <rhs> <-----------------|-`
		 *    <--------------------+
		 */
		uint32_t else_jmp, end_jmp[3] = { 0 };

		vm_compile_expr(wk, n->l);

		obj az_branches = 0;
		if (wk->vm.in_analyzer) {
			push_code(wk, op_az_branch);
			push_constant(wk, az_branch_type_normal);
			end_jmp[2] = wk->vm.code.len;
			push_constant(wk, 0);

			az_branches = make_obj(wk, obj_array);
			push_constant(wk, az_branches);
		}

		{ // if then branch
			if (wk->vm.in_analyzer) {
				obj_array_push(wk,
					az_branches,
					make_az_branch_element(wk, wk->vm.code.len, az_branch_element_flag_pop));
			}

			push_code(wk, op_jmp_if_disabler_keep);
			end_jmp[0] = wk->vm.code.len;
			push_constant(wk, 0);

			push_code(wk, op_jmp_if_false);
			else_jmp = wk->vm.code.len;
			push_constant(wk, 0);

			vm_compile_expr(wk, n->r->l);
			push_code(wk, op_jmp);
			end_jmp[1] = wk->vm.code.len;
			push_constant(wk, 0);
		}

		{ // else branch
			if (wk->vm.in_analyzer) {
				obj_array_push(wk,
					az_branches,
					make_az_branch_element(wk, wk->vm.code.len, az_branch_element_flag_pop));
			}
			push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, else_jmp));

			vm_compile_expr(wk, n->r->r);
		}

		push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, end_jmp[0]));
		push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, end_jmp[1]));

		if (wk->vm.in_analyzer) {
			push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, end_jmp[2]));
			push_code(wk, op_az_merge);
		}

		break;
	}
	case node_type_and:
	case node_type_or: {
		/* <lhs>
		 * jmp_if_disabler_keep >--,
		 * dup                     |
		 * jmp_if_(true/false) ----|-,
		 * pop                     | |
		 * <rhs>                   | |
		 * typecheck bool          | |
		 *    <--------------------+-`
		 */

		uint32_t jmp1, end_jmp[2] = { 0 };
		vm_compile_expr(wk, n->l);

		if (wk->vm.in_analyzer) {
			obj az_branches = 0;
			push_code(wk, op_az_branch);
			push_constant(wk, az_branch_type_normal);
			end_jmp[1] = wk->vm.code.len;
			push_constant(wk, 0);

			az_branches = make_obj(wk, obj_array);
			push_constant(wk, az_branches);

			obj_array_push(wk,
				az_branches,
				make_az_branch_element(wk, wk->vm.code.len, az_branch_element_flag_pop));
		}

		push_code(wk, op_jmp_if_disabler_keep);
		jmp1 = wk->vm.code.len;
		push_constant(wk, 0);

		push_code(wk, op_dup);

		if (n->type == node_type_and) {
			push_code(wk, op_jmp_if_false);
		} else {
			push_code(wk, op_jmp_if_true);
		}
		end_jmp[0] = wk->vm.code.len;
		push_constant(wk, 0);

		push_code(wk, op_pop);

		vm_compile_expr(wk, n->r);
		push_code(wk, op_typecheck);
		push_constant(wk, obj_bool);

		push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, jmp1));
		push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, end_jmp[0]));
		if (wk->vm.in_analyzer) {
			push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, end_jmp[1]));
		}

		if (wk->vm.in_analyzer) {
			push_code(wk, op_az_merge);
		}
		break;
	}
	case node_type_func_def: {
		obj f;
		uint32_t func_jump_over_patch_tgt, ndefargs = 0;
		struct obj_func *func;
		struct node *arg;

		f = make_obj(wk, obj_func);
		func = get_obj_func(wk, f);

		push_code(wk, op_jmp);
		func_jump_over_patch_tgt = wk->vm.code.len;
		push_constant(wk, 0);

		/* function body start */

		func->entry = wk->vm.code.len;
		func->return_type = n->data.type;

		vm_comp_push_call_frame(wk);

		for (arg = n->l->r; arg; arg = arg->r) {
			if (!arg->l) {
				break;
			}

			if (arg->l->type == node_type_kw) {
				vm_comp_declare_local(wk, arg->l->r, arg->l->r->data.str);
				++func->nkwargs;
			} else {
				vm_comp_declare_local(wk, arg->l, arg->l->data.str);
				++func->nargs;
			}

			struct local_binding *l = arr_peek(&wk->vm.compiler_state.locals, 1);
			l->bound = true;
		}

		vm_comp_block_locals(wk, n->r);

		vm_compile_block(wk, n->r, vm_compile_block_final_return);
		struct compiler_call_frame *frame = vm_comp_pop_call_frame(wk);
		vm_comp_init_func(wk, func, frame, func->nargs, func->nkwargs);

		/* function body end */

		push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, func_jump_over_patch_tgt));

		uint32_t kwarg_i = 0, arg_i = 0;
		for (arg = n->l->r; arg; arg = arg->r) {
			if (!arg->l) {
				break;
			}

			if (arg->l->type == node_type_kw) {
				struct node *doc = arg->l->r->r;
				func->akw[kwarg_i] = (struct args_kw){
					.key = get_cstr(wk, arg->l->r->data.str),
					.type = arg->l->r->l->data.type,
					.desc = doc ? get_cstr(wk, doc->data.str) : 0,
				};
				++kwarg_i;

				if (arg->l->l) {
					vm_compile_expr(wk, arg->l->l);
					push_code(wk, op_constant);
					push_constant(wk, arg->l->r->data.str);
					++ndefargs;
				}
			} else {
				struct node *doc = arg->l->r;
				func->an[arg_i] = (struct args_norm){
					.name = get_cstr(wk, arg->l->data.str),
					.type = arg->l->l->data.type,
					.desc = doc ? get_cstr(wk, doc->data.str) : 0,
				};
				++arg_i;
			}
		}

		if (ndefargs) {
			push_code(wk, op_constant_dict);
			push_constant(wk, ndefargs);
		} else {
			push_code(wk, op_constant);
			push_constant(wk, 0);
		}

		struct node *id = n->l->l->l;
		struct node *doc = n->l->l->r;
		push_location(wk, id ? id : n->l);

		func->def = wk->vm.code.len;

		push_code(wk, op_constant_func);
		push_constant(wk, f);

		if (id) {
			vm_comp_assign_local(wk, id, id->data.str, 0);
			func->name = get_str(wk, id->data.str)->s;
		}

		if (doc) {
			func->desc = get_str(wk, doc->data.str)->s;
		}
		break;
	}
	default: UNREACHABLE;
	}
}

static void
vm_compile_expr(struct workspace *wk, struct node *n)
{
	const enum node_type privelaged[] = {
		node_type_foreach,
		node_type_if,
		node_type_func_def,
		node_type_ternary,
		node_type_and,
		node_type_or,
	};

	vm_visit_nodes(wk, n, privelaged, ARRAY_LEN(privelaged), vm_comp_node);
}

static bool
vm_op_range_had_effect(struct workspace *wk, uint32_t start, uint32_t end)
{
	for (uint32_t i = start; i < end; i += OP_WIDTH(wk->vm.code.e[i])) {
		switch (wk->vm.code.e[i]) {
			case op_az_noop:
			case op_call:
			case op_call_native:
			case op_return:
			case op_store_g:
			case op_store_l:
			case op_store_u:
			case op_store_m:
			case op_add_store_g:
			case op_add_store_l:
			case op_add_store_u:
			case op_add_store_m:
				return true;
			default: break;
		}
	}
	return false;
}


static void
vm_compile_block(struct workspace *wk, struct node *n, enum vm_compile_block_flags flags)
{
	struct node *prev = 0;
	while (n && n->l) {
		assert(n->type == node_type_stmt);
		uint32_t expr_start = wk->vm.code.len;

		vm_compile_expr(wk, n->l);

		if (wk->vm.in_analyzer) {
			if (!vm_op_range_had_effect(wk, expr_start, wk->vm.code.len)) {
				vm_comp_warning(wk, n->l, "statment has no effect");
			}
		}

		if (n->l->type == node_type_if) {
			// don't pop
		} else if ((flags & vm_compile_block_expr) && !(n->r && n->r->l)) {
			// don't pop
		} else {
			push_code(wk, op_pop);
		}

		prev = n;
		n = n->r;
	}

	if (flags & vm_compile_block_final_return) {
		if (prev && prev->l->type == node_type_return) {
			--wk->vm.code.len;
			wk->vm.code.e[wk->vm.code.len - 1] = op_return_end;
		} else {
			push_code(wk, op_constant);
			push_constant(wk, 0);
			push_code(wk, op_return_end);
		}
	} else if (flags & vm_compile_block_expr) {
		push_code(wk, op_return_end);
	}
}

void
vm_compile_state_reset(struct workspace *wk)
{
	bucket_arr_clear(&wk->vm.compiler_state.nodes);
	wk->vm.compiler_state.breakpoints = 0;
}

static bool
vm_compile_breakpoint_to_off(struct workspace *wk, const struct source *src, obj bp, uint32_t *off)
{
	uint32_t t_line, t_col;
	vm_dbg_unpack_breakpoint(wk, bp, &t_line, &t_col);

	uint32_t i, line = 1, col = 1;
	for (i = 0; i < src->len; ++i) {
		if (line == t_line && (col == t_col || !t_col)) {
			*off = i;
			return true;
		}

		if (src->src[i] == '\n') {
			++line;
			col = 1;
		} else {
			++col;
		}
	}

	return false;
}

static void
vm_resolve_breakpoint_cb(struct workspace *wk, struct node *n)
{
	if (vm_comp_node_skip(n->type)) {
		return;
	}

	obj off, node;
	obj_dict_for(wk, wk->vm.compiler_state.breakpoints, off, node) {
		struct node *o = node ? (void *)(intptr_t)get_obj_number(wk, node) : 0;
		bool in_range = n->location.off <= off && off <= n->location.off + n->location.len;
		if (in_range && (!o || n->location.len < o->location.len)) {
			obj_dict_seti(wk, wk->vm.compiler_state.breakpoints, off, make_number(wk, (uintptr_t)n));
		}
	}
}

static void
vm_resolve_breakpoints(struct workspace *wk, struct node *n)
{
	const struct source *src = arr_peek(&wk->vm.src, 1);

	obj file_bp;
	if (obj_dict_index_str(wk, wk->vm.dbg_state.breakpoints, src->label, &file_bp)) {
		wk->vm.compiler_state.breakpoints = make_obj(wk, obj_dict);

		obj bp;
		obj_array_for(wk, file_bp, bp) {
			uint32_t off;
			if (vm_compile_breakpoint_to_off(wk, src, bp, &off)) {
				obj_dict_seti(wk, wk->vm.compiler_state.breakpoints, off, 0);
			} else {
				uint32_t t_line, t_col;
				vm_dbg_unpack_breakpoint(wk, bp, &t_line, &t_col);
				LOG_W("failed to resolve breakpoint %s:%d:%d", src->label, t_line, t_col);
			}
		}

		vm_visit_nodes(wk, n, 0, 0, vm_resolve_breakpoint_cb);

		obj off, node;
		obj_dict_for(wk, wk->vm.compiler_state.breakpoints, off, node) {
			struct node *o = node ? (void *)(intptr_t)get_obj_number(wk, node) : 0;
			if (!o) {
				LOG_W("failed to resolve breakpoint %s@%d", src->label, off);
				continue;
			}

			o->flags |= node_flag_breakpoint;
		}
	}
}

bool
vm_compile_ast(struct workspace *wk, struct node *n, enum vm_compile_mode mode, const struct args_norm *an, uint32_t *entry)
{
	TracyCZoneAutoS;
	if (mode & vm_compile_mode_language_extended) {
		mode |= vm_compile_mode_locals;
	}

	wk->vm.compiler_state.err = false;
	wk->vm.compiler_state.mode = mode;

	if (wk->vm.dbg_state.breakpoints) {
		vm_resolve_breakpoints(wk, n);
	}

	*entry = wk->vm.code.len;

	enum vm_compile_block_flags flags = vm_compile_block_final_return;
	if (mode & vm_compile_mode_expr) {
		flags &= ~vm_compile_block_final_return;
		flags |= vm_compile_block_expr;
	}

	uint32_t wrapper_func_args = 0;
	if (mode & vm_compile_mode_locals) {
		vm_comp_push_call_frame(wk);

		if (an) {
			struct local_binding *l;
			for (uint32_t i = 0; an[i].type != ARG_TYPE_NULL; ++i) {
				vm_comp_declare_local(wk, n, make_str(wk, an[i].name));
				l = arr_peek(&wk->vm.compiler_state.locals, 1);
				l->bound = true;
				l->accessed = true;
				++wrapper_func_args;
			}
		}

		vm_comp_block_locals(wk, n);
	}

	vm_compile_block(wk, n, flags);

	if (mode & vm_compile_mode_locals) {
		struct compiler_call_frame *frame = vm_comp_pop_call_frame(wk);

		uint32_t wrapper_func_entry = *entry;
		*entry = wk->vm.code.len;

		if (an) {
			for (uint32_t i = 0; an[i].type != ARG_TYPE_NULL; ++i) {
				push_code(wk, op_constant);
				push_constant(wk, an[i].val);
			}
		}

		push_code(wk, op_constant);
		push_constant(wk, 0);
		push_code(wk, op_constant_func);
		obj f = make_obj(wk, obj_func);
		struct obj_func *func = get_obj_func(wk, f);
		vm_comp_init_func(wk, func, frame, wrapper_func_args, 0);
		if (an) {
			for (uint32_t i = 0; an[i].type != ARG_TYPE_NULL; ++i) {
				func->an[i].type = tc_any;
			}
		}
		func->automatically_defined = true;
		func->def = wrapper_func_entry;
		func->entry = wrapper_func_entry;
		func->return_type = TYPE_TAG_ALLOW_NULL | tc_any;
		push_constant(wk, f);

		push_code(wk, op_call);
		push_constant(wk, func->nargs);
		push_constant(wk, func->nkwargs);

		push_code(wk, op_return_end);
	}

	assert(wk->vm.compiler_state.node_stack.len == 0);
	assert(wk->vm.compiler_state.locals.len == 0);
	assert(wk->vm.compiler_state.upvalues.len == 0);
	assert(wk->vm.compiler_state.call_stack.len == 0);
	assert(wk->vm.compiler_state.loop_jmp_stack.len == 0);
	assert(wk->vm.compiler_state.if_jmp_stack.len == 0);

	TracyCZoneAutoE;
	return !wk->vm.compiler_state.err;
}

bool
vm_compile(struct workspace *wk, const struct source *src, enum vm_compile_mode mode, uint32_t *entry)
{
	struct node *n;

	vm_compile_state_reset(wk);

	if (!(n = parse(wk, src, mode))) {
		wk->vm.compiler_state.err = true;
		return false;
	}

	return vm_compile_ast(wk, n, mode, 0, entry);
}
