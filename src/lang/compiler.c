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
push_op_store(struct workspace *wk, enum op_store_flags flags)
{
	push_code(wk, op_store);
	push_constant(wk, flags);
}

static void vm_comp_error(struct workspace *wk, struct node *n, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);

static void
vm_comp_error(struct workspace *wk, struct node *n, const char *fmt, ...)
{
	struct source *src = arr_peek(&wk->vm.src, 1);

	va_list args;
	va_start(args, fmt);
	error_messagev(wk, src, n->location, log_error, fmt, args);
	va_end(args);

	wk->vm.compiler_state.err = true;
}

static void vm_comp_warning(struct workspace *wk, struct node *n, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);

static void
vm_comp_warning(struct workspace *wk, struct node *n, const char *fmt, ...)
{
	struct source *src = arr_peek(&wk->vm.src, 1);

	va_list args;
	va_start(args, fmt);
	error_messagev(wk, src, n->location, log_warn, fmt, args);
	va_end(args);
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

void
vm_comp_op_return(struct workspace *wk)
{
	if (!wk->vm.in_analyzer) {
		for (uint32_t i = 0; i < wk->vm.compiler_state.loop_depth; ++i) {
			push_code(wk, op_swap);
			push_code(wk, op_pop);
		}
	}

	push_code(wk, op_return);
}

enum vm_compile_block_flags {
	vm_compile_block_final_return = 1 << 0,
	vm_compile_block_expr = 1 << 1,
	vm_compile_block_start_scope = 1 << 2,
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
	case node_type_id:
		push_code(wk, op_constant);
		push_constant(wk, n->data.str);
		push_code(wk, op_load);
		break;
	case node_type_maybe_id:
		push_code(wk, op_constant);
		push_constant(wk, n->data.str);
		push_code(wk, op_dup);
		push_code(wk, op_try_load);
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
	case node_type_assign:
		if (!(n->data.type & op_store_flag_member)) {
			switch (n->l->type) {
			case node_type_id_lit:
				push_code(wk, op_constant);
				push_constant(wk, n->l->data.str);
				break;
			default: vm_compile_expr(wk, n->l); break;
			}
		}
		push_op_store(wk, n->data.type);
		break;
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
				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 0, 0, 0);

				push_code(wk, op_constant);
				push_constant(wk, 0);

				vm_comp_op_return(wk);
				break;
			} else if (str_eql(name, &STR("set_variable"))) {
				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 2, 2, 0);

				push_code(wk, op_swap);
				push_op_store(wk, 0);
				break;
			} else if (str_eql(name, &STR("get_variable"))) {
				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 1, 2, 0);

				if (n->l->data.len.args == 1) {
					push_code(wk, op_load);
				} else {
					push_code(wk, op_try_load);
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

		vm_comp_op_return(wk);
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

		push_code(wk, op_constant);
		push_constant(wk, ida->data.str);
		push_op_store(wk, 0);
		push_code(wk, op_pop);

		if (idb) {
			push_code(wk, op_constant);
			push_constant(wk, idb->data.str);
			push_op_store(wk, 0);
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
		push_constant(wk, *(uint32_t *)arr_peek(&wk->vm.compiler_state.loop_jmp_stack, 1));
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

		vm_compile_block(wk, n->r, vm_compile_block_final_return | vm_compile_block_start_scope);

		/* function body end */

		push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, func_jump_over_patch_tgt));

		for (arg = n->l->r; arg; arg = arg->r) {
			if (!arg->l) {
				break;
			}

			if (arg->l->type == node_type_kw) {
				struct node *doc = arg->l->r->r;

				func->akw[func->nkwargs] = (struct args_kw){
					.key = get_cstr(wk, arg->l->r->data.str),
					.type = arg->l->r->l->data.type,
					.desc = doc ? get_cstr(wk, doc->data.str) : 0,
				};
				++func->nkwargs;

				if (arg->l->l) {
					vm_compile_expr(wk, arg->l->l);
					push_code(wk, op_constant);
					push_constant(wk, arg->l->r->data.str);
					++ndefargs;
				}
			} else {
				struct node *doc = arg->l->r;

				func->an[func->nargs] = (struct args_norm){
					.name = get_cstr(wk, arg->l->data.str),
					.type = arg->l->l->data.type,
					.desc = doc ? get_cstr(wk, doc->data.str) : 0,
				};
				++func->nargs;
			}
		}
		func->an[func->nargs].type = ARG_TYPE_NULL;
		func->akw[func->nkwargs].key = 0;
		func->return_type = n->data.type;
		func->lang_mode = wk->vm.lang_mode;

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
			push_code(wk, op_constant);
			push_constant(wk, id->data.str);
			push_op_store(wk, 0);
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
			case op_store:
				return true;
			default: break;
		}
	}
	return false;
}

static void
vm_compile_block(struct workspace *wk, struct node *n, enum vm_compile_block_flags flags)
{
	if (flags & vm_compile_block_start_scope) {
		stack_push(&wk->stack, wk->vm.compiler_state.loop_depth, 0);
	}

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

	if (flags & vm_compile_block_start_scope) {
		stack_pop(&wk->stack, wk->vm.compiler_state.loop_depth);
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
vm_compile_initial_code_segment(struct workspace *wk)
{
	arr_push(wk->a, &wk->vm.locations,
		&(struct source_location_mapping){
			.ip = 0,
			.loc = { 0 },
			.src_idx = UINT32_MAX,
		});

	push_code(wk, op_constant);
	push_constant(wk, 0);
	push_code(wk, op_return_end);
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
vm_compile_ast(struct workspace *wk, struct node *n, enum vm_compile_mode mode, uint32_t *entry)
{
	TracyCZoneAutoS;
	wk->vm.compiler_state.err = false;
	wk->vm.compiler_state.mode = mode;

	if (wk->vm.dbg_state.breakpoints) {
		vm_resolve_breakpoints(wk, n);
	}

	*entry = wk->vm.code.len;

	enum vm_compile_block_flags flags = vm_compile_block_final_return | vm_compile_block_start_scope;
	if (mode & vm_compile_mode_expr) {
		flags &= ~vm_compile_block_final_return;
		flags |= vm_compile_block_expr;
	}

	vm_compile_block(wk, n, flags);

	assert(wk->vm.compiler_state.node_stack.len == 0);
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
	}

	return vm_compile_ast(wk, n, mode, entry);
}
