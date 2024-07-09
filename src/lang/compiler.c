/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <string.h>

#include "buf_size.h"
#include "lang/analyze.h"
#include "lang/compiler.h"
#include "lang/func_lookup.h"
#include "lang/parser.h"
#include "lang/typecheck.h"
#include "lang/vm.h"
#include "lang/workspace.h"
#include "platform/path.h"
#include "tracy.h"

/******************************************************************************
 * compiler
 ******************************************************************************/

static void
push_location(struct workspace *wk, struct node *n)
{
	arr_push(&wk->vm.locations,
		&(struct source_location_mapping){
			.ip = wk->vm.code.len,
			.loc = n->location,
			.src_idx = wk->vm.src.len - 1,
		});
}

static void
push_code(struct workspace *wk, uint8_t b)
{
	arr_push(&wk->vm.code, &b);
}

static void
push_constant_at(obj v, uint8_t *code)
{
	code[0] = (v >> 16) & 0xff;
	code[1] = (v >> 8) & 0xff;
	code[2] = v & 0xff;
}

static void
push_constant(struct workspace *wk, obj v)
{
	push_code(wk, (v >> 16) & 0xff);
	push_code(wk, (v >> 8) & 0xff);
	push_code(wk, v & 0xff);
}

static void vm_comp_error(struct workspace *wk, struct node *n, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);
static void
vm_comp_error(struct workspace *wk, struct node *n, const char *fmt, ...)
{
	struct source *src = arr_peek(&wk->vm.src, 1);

	va_list args;
	va_start(args, fmt);
	error_messagev(src, n->location, log_error, fmt, args);
	va_end(args);

	wk->vm.compiler_state.err = true;
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

static void vm_compile_block(struct workspace *wk, struct node *n, bool final_return);
static void vm_compile_expr(struct workspace *wk, struct node *n);

static void
vm_comp_node(struct workspace *wk, struct node *n)
{
	assert(n->type != node_type_stmt);

	/* L("compiling %s", node_to_s(wk, n)); */

	switch (n->type) {
	case node_type_group:
	case node_type_stmt: UNREACHABLE;

	case node_type_id_lit:
	case node_type_args:
	case node_type_def_args:
	case node_type_kw:
	case node_type_list:
	case node_type_foreach_args:
		// Skipped
		break;

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
	case node_type_number:
		push_code(wk, op_constant);
		obj o;
		make_obj(wk, &o, obj_number);
		set_obj_number(wk, o, n->data.num);
		push_constant(wk, o);
		break;
	case node_type_bool:
		push_code(wk, op_constant);
		push_constant(wk, n->data.num ? obj_bool_true : obj_bool_false);
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
		push_code(wk, op_constant);
		push_constant(wk, n->l->data.str);
		push_code(wk, op_store);
		break;
	case node_type_plusassign:
		push_code(wk, op_add_store);
		push_constant(wk, n->l->data.str);
		break;
	case node_type_method: {
		push_code(wk, op_call_method);
		push_constant(wk, n->r->data.str);
		push_constant(wk, n->l->data.len.args);
		push_constant(wk, n->l->data.len.kwargs);
		break;
	}
	case node_type_call: {
		bool known = false;
		uint32_t idx;

		if (n->r->type == node_type_id_lit) {
			const struct str *name = get_str(wk, n->r->data.str);
			if (str_eql(name, &WKSTR("subdir_done"))) {
				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 0, 0, 0);

				push_code(wk, op_constant);
				push_constant(wk, 0);
				push_code(wk, op_return);
				break;
			} else if (str_eql(name, &WKSTR("set_variable"))) {
				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 2, 2, 0);

				push_code(wk, op_swap);
				push_code(wk, op_store);
				break;
			} else if (str_eql(name, &WKSTR("get_variable"))) {
				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 1, 2, 0);

				if (n->l->data.len.args == 1) {
					push_code(wk, op_load);
				} else {
					push_code(wk, op_try_load);
				}
				break;
			} else if (str_eql(name, &WKSTR("disabler"))) {
				push_location(wk, n);

				vm_comp_assert_inline_func_args(wk, n, n->l, 0, 0, 0);

				push_code(wk, op_constant);
				push_constant(wk, disabler_id);
				break;
			} else if (str_eql(name, &WKSTR("is_disabler"))) {
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

		uint32_t az_merge_point_tgt;
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
		push_code(wk, op_store);
		push_code(wk, op_pop);

		if (idb) {
			push_code(wk, op_constant);
			push_constant(wk, idb->data.str);
			push_code(wk, op_store);
			push_code(wk, op_pop);
		}

		uint32_t loop_jmp_stack_base = wk->vm.compiler_state.loop_jmp_stack.len;
		arr_push(&wk->vm.compiler_state.loop_jmp_stack, &loop_body_start);

		vm_compile_block(wk, n->r, 0);

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
			push_code(wk, op_constant);
			push_constant(wk, 0);
			break;
		}

		push_code(wk, op_jmp);
		push_constant(wk, *(uint32_t *)arr_peek(&wk->vm.compiler_state.loop_jmp_stack, 1));
		break;
	}
	case node_type_break: {
		if (wk->vm.in_analyzer) {
			push_code(wk, op_constant);
			push_constant(wk, 0);
			break;
		}

		push_code(wk, op_jmp);

		uint32_t top = *(uint32_t *)arr_pop(&wk->vm.compiler_state.loop_jmp_stack);
		arr_push(&wk->vm.compiler_state.loop_jmp_stack, &wk->vm.code.len);
		arr_push(&wk->vm.compiler_state.loop_jmp_stack, &top);

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

			arr_push(&wk->vm.compiler_state.if_jmp_stack, &wk->vm.code.len);
			++patch_tgts;
			push_constant(wk, 0);

			make_obj(wk, &az_branches, obj_array);
			push_constant(wk, az_branches);
		}

		while (n) {
			if (wk->vm.in_analyzer) {
				obj_array_push(wk, az_branches, make_az_branch_element(wk, wk->vm.code.len, 0));
			}

			if (n->l->l) {
				vm_compile_expr(wk, n->l->l);
				push_code(wk, op_jmp_if_disabler);
				arr_push(&wk->vm.compiler_state.if_jmp_stack, &wk->vm.code.len);
				++patch_tgts;
				push_constant(wk, 0);

				push_code(wk, op_jmp_if_false);
				else_jmp = wk->vm.code.len;
				push_constant(wk, 0);
			}

			vm_compile_block(wk, n->l->r, 0);

			push_code(wk, op_jmp);
			arr_push(&wk->vm.compiler_state.if_jmp_stack, &wk->vm.code.len);
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
		uint32_t else_jmp, end_jmp[3];

		vm_compile_expr(wk, n->l);

		obj az_branches = 0;
		if (wk->vm.in_analyzer) {
			push_code(wk, op_az_branch);
			push_constant(wk, az_branch_type_normal);
			end_jmp[2] = wk->vm.code.len;
			push_constant(wk, 0);

			make_obj(wk, &az_branches, obj_array);
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

		uint32_t jmp1, end_jmp[2];
		vm_compile_expr(wk, n->l);

		if (wk->vm.in_analyzer) {
			obj az_branches = 0;
			push_code(wk, op_az_branch);
			push_constant(wk, az_branch_type_normal);
			end_jmp[1] = wk->vm.code.len;
			push_constant(wk, 0);

			make_obj(wk, &az_branches, obj_array);
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

		make_obj(wk, &f, obj_func);
		func = get_obj_func(wk, f);

		push_code(wk, op_jmp);
		func_jump_over_patch_tgt = wk->vm.code.len;
		push_constant(wk, 0);

		/* function body start */

		func->entry = wk->vm.code.len;

		vm_compile_block(wk, n->r, true);

		/* function body end */

		push_constant_at(wk->vm.code.len, arr_get(&wk->vm.code, func_jump_over_patch_tgt));

		for (arg = n->l->r; arg; arg = arg->r) {
			if (!arg->l) {
				break;
			}

			if (arg->l->type == node_type_kw) {
				func->akw[func->nkwargs] = (struct args_kw){
					.key = get_cstr(wk, arg->l->r->data.str),
					.type = arg->l->r->l->data.type,
				};
				++func->nkwargs;

				if (arg->l->l) {
					vm_compile_expr(wk, arg->l->l);
					push_code(wk, op_constant);
					push_constant(wk, arg->l->r->data.str);
					++ndefargs;
				}
			} else {
				func->an[func->nargs] = (struct args_norm){
					.name = get_cstr(wk, arg->l->data.str),
					.type = arg->l->l->data.type,
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

		push_location(wk, n->l->l);

		push_code(wk, op_constant_func);
		push_constant(wk, f);

		push_code(wk, op_constant);
		push_constant(wk, n->l->l->data.str);
		push_code(wk, op_store);

		break;
	}
	}
}

static void
vm_compile_expr(struct workspace *wk, struct node *n)
{
	struct node *peek, *prev = 0;

	uint32_t stack_base = wk->vm.compiler_state.node_stack.len;

	while (wk->vm.compiler_state.node_stack.len > stack_base || n) {
		if (n) {
			if (n->type == node_type_foreach || n->type == node_type_if || n->type == node_type_func_def
				|| n->type == node_type_ternary || n->type == node_type_and
				|| n->type == node_type_or) {
				vm_comp_node(wk, n);
				prev = n;
				n = 0;
			} else {
				arr_push(&wk->vm.compiler_state.node_stack, &n);
				n = n->l;
			}
		} else {
			peek = *(struct node **)arr_peek(&wk->vm.compiler_state.node_stack, 1);
			if (peek->r && prev != peek->r) {
				n = peek->r;
			} else {
				push_location(wk, peek);
				vm_comp_node(wk, peek);
				prev = *(struct node **)arr_pop(&wk->vm.compiler_state.node_stack);
			}
		}
	}
}

static void
vm_compile_block(struct workspace *wk, struct node *n, bool final_return)
{
	struct node *prev = 0;
	while (n && n->l) {
		assert(n->type == node_type_stmt);
		vm_compile_expr(wk, n->l);

		if (n->l->type != node_type_if) {
			push_code(wk, op_pop);
		}

		prev = n;
		n = n->r;
	}

	if (final_return) {
		if (prev && prev->l->type == node_type_return) {
			--wk->vm.code.len;
			wk->vm.code.e[wk->vm.code.len - 1] = op_return_end;
		} else {
			push_code(wk, op_constant);
			push_constant(wk, 0);
			push_code(wk, op_return_end);
		}
	}
}

void
vm_compile_initial_code_segment(struct workspace *wk)
{
	arr_push(&wk->vm.locations,
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
}

bool
vm_compile_ast(struct workspace *wk, struct node *n, enum vm_compile_mode mode, uint32_t *entry)
{
	TracyCZoneAutoS;
	wk->vm.compiler_state.err = false;

	*entry = wk->vm.code.len;

	vm_compile_block(wk, n, true);

	assert(wk->vm.compiler_state.node_stack.len == 0);
	assert(wk->vm.compiler_state.loop_jmp_stack.len == 0);
	assert(wk->vm.compiler_state.if_jmp_stack.len == 0);

	TracyCZoneAutoE;
	return !wk->vm.compiler_state.err;
}

bool
vm_compile(struct workspace *wk, struct source *src, enum vm_compile_mode mode, uint32_t *entry)
{
	struct node *n;

	vm_compile_state_reset(wk);

	if (!(n = parse(wk, src, mode))) {
		wk->vm.compiler_state.err = true;
	}

	return vm_compile_ast(wk, n, mode, entry);
}
