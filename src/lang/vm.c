/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <string.h>

#include "coerce.h"
#include "error.h"
#include "lang/analyze.h"
#include "lang/compiler.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/parser.h"
#include "lang/typecheck.h"
#include "lang/vm.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/init.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "tracy.h"

const uint32_t op_operands[op_count] = {
	[op_iterator] = 1,
	[op_iterator_next] = 1,
	[op_add_store] = 1,
	[op_constant] = 1,
	[op_constant_list] = 1,
	[op_constant_dict] = 1,
	[op_constant_func] = 1,
	[op_call] = 2,
	[op_call_method] = 3,
	[op_call_native] = 3,
	[op_jmp_if_true] = 1,
	[op_jmp_if_false] = 1,
	[op_jmp_if_disabler] = 1,
	[op_jmp_if_disabler_keep] = 1,
	[op_jmp] = 1,
	[op_typecheck] = 1,
	[op_az_branch] = 3,
};
const uint32_t op_operand_size = 3;

/******************************************************************************
 * object stack
 ******************************************************************************/

enum { object_stack_page_size = 1024 / sizeof(struct obj_stack_entry) };

static void
object_stack_alloc_page(struct object_stack *s)
{
	bucket_arr_pushn(&s->ba, 0, 0, object_stack_page_size);
	s->ba.len -= object_stack_page_size;
	++s->bucket;
	s->page = (struct obj_stack_entry *)((struct bucket *)s->ba.buckets.e)[s->bucket].mem;
	((struct bucket *)s->ba.buckets.e)[s->bucket].len = object_stack_page_size;
	s->i = 0;
}

static void
object_stack_init(struct object_stack *s)
{
	bucket_arr_init(&s->ba, object_stack_page_size, sizeof(struct obj_stack_entry));
	s->page = (struct obj_stack_entry *)((struct bucket *)s->ba.buckets.e)[0].mem;
	((struct bucket *)s->ba.buckets.e)[0].len = object_stack_page_size;
}

static void
object_stack_push_ip(struct workspace *wk, obj o, uint32_t ip)
{
	if (wk->vm.stack.i >= object_stack_page_size) {
		object_stack_alloc_page(&wk->vm.stack);
	}

	wk->vm.stack.page[wk->vm.stack.i] = (struct obj_stack_entry){ .o = o, .ip = ip };
	++wk->vm.stack.i;
	++wk->vm.stack.ba.len;
}

void
object_stack_push(struct workspace *wk, obj o)
{
	object_stack_push_ip(wk, o, wk->vm.ip - 1);
}

struct obj_stack_entry *
object_stack_pop_entry(struct object_stack *s)
{
	if (!s->i) {
		assert(s->bucket);
		--s->bucket;
		s->page = (struct obj_stack_entry *)((struct bucket *)s->ba.buckets.e)[s->bucket].mem;
		s->i = object_stack_page_size;
	}

	--s->i;
	--s->ba.len;
	return &s->page[s->i];
}

obj
object_stack_pop(struct object_stack *s)
{
	return object_stack_pop_entry(s)->o;
}

struct obj_stack_entry *
object_stack_peek_entry(struct object_stack *s, uint32_t off)
{
	return bucket_arr_get(&s->ba, s->ba.len - off);
}

obj
object_stack_peek(struct object_stack *s, uint32_t off)
{
	return ((struct obj_stack_entry *)bucket_arr_get(&s->ba, s->ba.len - off))->o;
}

void
object_stack_discard(struct object_stack *s, uint32_t n)
{
	assert(s->ba.len >= n);
	s->ba.len -= n;
	s->bucket = s->ba.len / s->ba.bucket_size;
	s->page = (struct obj_stack_entry *)((struct bucket *)s->ba.buckets.e)[s->bucket].mem;
	s->i = s->ba.len % s->ba.bucket_size;
}

void
object_stack_print(struct workspace *wk, struct object_stack *s)
{
	for (int32_t i = s->ba.len - 1; i >= 0; --i) {
		struct obj_stack_entry *e = bucket_arr_get(&s->ba, i);
		obj_fprintf(wk, log_file(), "%o%s", e->o, i > 0 ? ", " : "");
	}
	log_plain("\n");
}

/******************************************************************************
 * vm errors
 ******************************************************************************/

void
vm_lookup_inst_location_src_idx(struct vm *vm, uint32_t ip, struct source_location *loc, uint32_t *src_idx)
{
	struct source_location_mapping *locations = (struct source_location_mapping *)vm->locations.e;

	uint32_t i;
	for (i = 0; i < vm->locations.len; ++i) {
		if (locations[i].ip > ip) {
			if (i) {
				--i;
			}
			break;
		}
	}

	if (i == vm->locations.len) {
		--i;
	}

	*loc = locations[i].loc;
	*src_idx = locations[i].src_idx;
}

static void
vm_lookup_inst_location(struct vm *vm, uint32_t ip, struct source_location *loc, struct source **src)
{
	uint32_t src_idx;
	vm_lookup_inst_location_src_idx(vm, ip, loc, &src_idx);

	if (src_idx == UINT32_MAX) {
		static struct source null_src = { 0 };
		*src = &null_src;
	} else {
		*src = arr_get(&vm->src, src_idx);
	}
}

void
vm_diagnostic_v(struct workspace *wk, uint32_t ip, enum log_level lvl, const char *fmt, va_list args)
{
	static char buf[1024];
	obj_vsnprintf(wk, buf, ARRAY_LEN(buf), fmt, args);

	if (!ip) {
		ip = wk->vm.ip - 1;
	}

	struct source_location loc;
	struct source *src;
	vm_lookup_inst_location(&wk->vm, ip, &loc, &src);

	error_message(src, loc, lvl, buf);

	if (lvl == log_error) {
		if (wk->vm.in_analyzer) {
			az_set_error();
		} else {
			wk->vm.error = true;
			wk->vm.run = false;
		}
	}
}

void
vm_diagnostic(struct workspace *wk, uint32_t ip, enum log_level lvl, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, ip, lvl, fmt, args);
	va_end(args);
}

void
vm_error_at(struct workspace *wk, uint32_t ip, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, ip, log_error, fmt, args);
	va_end(args);
}

void
vm_warning(struct workspace *wk, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, 0, log_warn, fmt, args);
	va_end(args);
}

void
vm_warning_at(struct workspace *wk, uint32_t ip, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, ip, log_warn, fmt, args);
	va_end(args);
}

void
vm_error(struct workspace *wk, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, 0, log_error, fmt, args);
	va_end(args);
}

/******************************************************************************
 * pop_args
 ******************************************************************************/
static bool
typecheck_function_arg(struct workspace *wk, uint32_t ip, obj val, type_tag type)
{
	bool listify = (type & TYPE_TAG_LISTIFY) == TYPE_TAG_LISTIFY;
	type &= ~TYPE_TAG_LISTIFY;
	enum obj_type t = get_obj_type(wk, val);
	obj v;

	if (listify && t == obj_array) {
		obj_array_flat_for_(wk, val, v, flat_iter) {
			if (typecheck_typeinfo(wk, v, tc_array)) {
				continue;
			} else if (!typecheck(wk, ip, v, type)) {
				obj_array_flat_iter_end(wk, &flat_iter);
				return false;
			}
		}

		return true;
	} else if (listify && typecheck_typeinfo(wk, val, tc_array)) {
		return true;
	}

	return typecheck(wk, ip, val, type);
}

static bool
typecheck_and_mutate_function_arg(struct workspace *wk, uint32_t ip, obj *val, type_tag type)
{
	bool listify = (type & TYPE_TAG_LISTIFY) == TYPE_TAG_LISTIFY;
	type &= ~TYPE_TAG_LISTIFY;

	enum obj_type t = get_obj_type(wk, *val);

	// If obj_file or tc_file is requested, and the argument is an array of
	// length 1, try to unpack it.
	if (!listify && (type == obj_file || (type & tc_file) == tc_file)) {
		if (t == obj_array && get_obj_array(wk, *val)->len == 1) {
			obj i0;
			obj_array_index(wk, *val, 0, &i0);
			if (get_obj_type(wk, i0) == obj_file) {
				*val = i0;
			}
		} else if (t == obj_typeinfo && typecheck_typeinfo(wk, *val, tc_array)) {
			return true;
		}
	}

	if (listify) {
		obj v, arr;
		make_obj(wk, &arr, obj_array);

		if (t == obj_array) {
			obj_array_flat_for_(wk, *val, v, flat_iter) {
				if (v == disabler_id) {
					wk->vm.saw_disabler = true;
				} else if (typecheck_typeinfo(wk, v, tc_array)) {
					// Consider a function with signature:
					//   func f(a listify[str])
					// Now, imagine it gets called like this:
					//   f(['a', ['b'], <typeinfo: array>, 'd'])
					// When flattening this array, it is
					// impossible to flatten the typeinfo
					// array because it lacks any
					// information about what might be
					// inside it, or even if it is empty or
					// not.
					//
					// Also, consider this:
					//   f(['a', ['b'], <typeinfo: any>, 'd'])
					//
					// Now, the typeinfo could be anything
					// (including an array) so we still
					// don't know whether to flatten it.
					//
					// For now, just let these values
					// through unscathed.
				} else if (!typecheck(wk, ip, v, type)) {
					obj_array_flat_iter_end(wk, &flat_iter);
					return false;
				}

				obj_array_push(wk, arr, v);
			}
		} else if (t == obj_typeinfo && typecheck_typeinfo(wk, *val, tc_array)) {
			return true;
		} else {
			if (*val == disabler_id) {
				wk->vm.saw_disabler = true;
			} else if (!typecheck(wk, ip, *val, type)) {
				return false;
			}

			obj_array_push(wk, arr, *val);
		}

		*val = arr;
		return true;
	}

	return typecheck(wk, ip, *val, type);
}

static bool
handle_kwarg(struct workspace *wk, struct args_kw akw[], const char *kw, uint32_t kw_ip, obj v, uint32_t v_ip)
{
	uint32_t i;

	for (i = 0; akw[i].key; ++i) {
		if (strcmp(kw, akw[i].key) == 0) {
			break;
		}
	}

	if (!akw[i].key) {
		vm_diagnostic(wk, kw_ip, log_error, "unknown kwarg %s", kw);
		return false;
	} else if (akw[i].set) {
		vm_error_at(wk, kw_ip, "keyword argument '%s' set twice", kw);
		return false;
	}

	if (!typecheck_and_mutate_function_arg(wk, v_ip, &v, akw[i].type)) {
		return false;
	}

	akw[i].val = v;
	akw[i].node = v_ip;
	akw[i].set = true;
	return true;
}

bool
vm_pop_args(struct workspace *wk, struct args_norm an[], struct args_kw akw[])
{
	const char *kw;
	struct obj_stack_entry *entry;
	uint32_t i, j, argi;
	uint32_t args_popped = 0;
	bool got_kwargs_typeinfo = false;

	if (wk->vm.dbg_state.dump_signature) {
		dump_function_signature(wk, an, akw);
		return false;
	}

	if (akw) {
		for (i = 0; akw[i].key; ++i) {
			akw[i].set = false;
		}
	} else if (wk->vm.nkwargs) {
		vm_error(wk, "this function does not accept kwargs");
		goto err;
	}

	for (i = 0; i < wk->vm.nkwargs; ++i) {
		entry = object_stack_pop_entry(&wk->vm.stack);
		++args_popped;
		kw = get_str(wk, entry->o)->s;
		if (strcmp(kw, "kwargs") == 0) {
			entry = object_stack_pop_entry(&wk->vm.stack);
			++args_popped;
			if (entry->o == disabler_id) {
				wk->vm.saw_disabler = true;
				continue;
			}

			if (!typecheck(wk, entry->ip, entry->o, obj_dict)) {
				goto err;
			}

			if (get_obj_type(wk, entry->o) == obj_typeinfo) {
				// If we get kwargs as typeinfo, we can't do
				// anything useful
				got_kwargs_typeinfo = true;
				continue;
			}

			obj k, v;
			obj_dict_for(wk, entry->o, k, v) {
				if (!handle_kwarg(wk, akw, get_cstr(wk, k), entry->ip, v, entry->ip)) {
					goto err;
				}
				wk->vm.saw_disabler |= v == disabler_id;
			}
		} else {
			uint32_t kw_ip = entry->ip;
			entry = object_stack_pop_entry(&wk->vm.stack);
			++args_popped;
			if (!handle_kwarg(wk, akw, kw, kw_ip, entry->o, entry->ip)) {
				goto err;
			}
			wk->vm.saw_disabler |= entry->o == disabler_id;
		}
	}

	if (akw && !got_kwargs_typeinfo) {
		for (i = 0; akw[i].key; ++i) {
			if (akw[i].required && !akw[i].set) {
				vm_error(wk, "missing required keyword argument: %s", akw[i].key);
				goto err;
			}
		}
	}

	argi = 0;

	for (i = 0; an && an[i].type != ARG_TYPE_NULL; ++i) {
		an[i].set = false;
		type_tag type = an[i].type;

		if (type & TYPE_TAG_GLOB) {
			type &= ~TYPE_TAG_GLOB;
			type |= TYPE_TAG_LISTIFY;
			an[i].set = true;
			make_obj(wk, &an[i].val, obj_array);
			for (j = i; j < wk->vm.nargs; ++j) {
				entry = object_stack_peek_entry(&wk->vm.stack, wk->vm.nargs - argi);
				wk->vm.saw_disabler |= entry->o == disabler_id;
				obj_array_push(wk, an[i].val, entry->o);
				an[i].node = entry->ip;
				++argi;

				if (!typecheck_function_arg(wk, entry->ip, entry->o, type)) {
					goto err;
				}
			}
		} else {
			if (argi >= wk->vm.nargs) {
				if (!an[i].optional) {
					vm_error(wk,
						"missing positional argument%s%s",
						an[i].name ? ": " : "",
						an[i].name ? an[i].name : "");
					goto err;
				} else {
					break;
				}
			}

			entry = object_stack_peek_entry(&wk->vm.stack, wk->vm.nargs - argi);
			wk->vm.saw_disabler |= entry->o == disabler_id;
			an[i].val = entry->o;
			an[i].node = entry->ip;
			an[i].set = true;
			++argi;
		}

		if (!typecheck_and_mutate_function_arg(wk, an[i].node, &an[i].val, type)) {
			goto err;
		}
	}

	if (wk->vm.nargs > argi) {
		vm_error(wk, "too many args, got %d, expected %d", wk->vm.nargs, argi);
		goto err;
	}

	object_stack_discard(&wk->vm.stack, argi);
	args_popped += argi;

	if (wk->vm.saw_disabler) {
		goto err;
	}

	return true;
err:
	object_stack_discard(&wk->vm.stack, (wk->vm.nargs + wk->vm.nkwargs * 2) - args_popped);
	return false;
}

bool
pop_args(struct workspace *wk, struct args_norm an[], struct args_kw akw[])
{
	return wk->vm.behavior.pop_args(wk, an, akw);
}

/******************************************************************************
 * utility functions
 ******************************************************************************/

obj
vm_get_constant(uint8_t *code, uint32_t *ip)
{
	obj r = (code[*ip + 0] << 16) | (code[*ip + 1] << 8) | code[*ip + 2];
	*ip += 3;
	return r;
}

/******************************************************************************
 * disassembler
 ******************************************************************************/

const char *
vm_dis_inst(struct workspace *wk, uint8_t *code, uint32_t base_ip)
{
	uint32_t i = 0;
	static char buf[2048];
	buf[0] = 0;
#define buf_push(...) i += obj_snprintf(wk, &buf[i], sizeof(buf) - i, __VA_ARGS__);
#define op_case(__op) \
	case __op: buf_push(#__op);

	uint32_t ip = base_ip;
	buf_push("%04x ", ip);

	// clang-format off
	switch ((enum op)code[ip++]) {
	op_case(op_pop) break;
	op_case(op_dup) break;
	op_case(op_swap) break;
	op_case(op_stringify) break;
	op_case(op_index) break;
	op_case(op_add) break;
	op_case(op_sub) break;
	op_case(op_mul) break;
	op_case(op_div) break;
	op_case(op_mod) break;
	op_case(op_eq) break;
	op_case(op_in) break;
	op_case(op_gt) break;
	op_case(op_lt) break;
	op_case(op_not) break;
	op_case(op_negate) break;
	op_case(op_return) break;
	op_case(op_return_end) break;
	op_case(op_store) break;
	op_case(op_try_load) break;
	op_case(op_load) break;

	op_case(op_iterator)
		buf_push(":%d", vm_get_constant(code, &ip));
		break;
	op_case(op_iterator_next)
		buf_push(":%04x", vm_get_constant(code, &ip));
		break;
	op_case(op_add_store)
		buf_push(":%s", get_str(wk, vm_get_constant(code, &ip))->s);
		break;
	op_case(op_constant)
		buf_push(":%o", vm_get_constant(code, &ip));
		break;
	op_case(op_constant_list)
		buf_push(":len:%d", vm_get_constant(code, &ip));
		break;
	op_case(op_constant_dict)
		buf_push(":len:%d", vm_get_constant(code, &ip));
		break;
	op_case(op_constant_func)
		buf_push(":%d", vm_get_constant(code, &ip));
		break;
	op_case(op_call)
		buf_push(":%d,%d", vm_get_constant(code, &ip), vm_get_constant(code, &ip));
		break;
	op_case(op_call_method) {
		uint32_t a, b, c;
		a = vm_get_constant(code, &ip);
		b = vm_get_constant(code, &ip);
		c = vm_get_constant(code, &ip);
		buf_push(":%o,%d,%d", a, b, c);
		break;
	}
	op_case(op_call_native)
		buf_push(":");
		buf_push("%d,%d,", vm_get_constant(code, &ip), vm_get_constant(code, &ip));
		uint32_t id = vm_get_constant(code, &ip);
		buf_push("%s", native_funcs[id].name);
		break;
	op_case(op_jmp_if_true)
		buf_push(":%04x", vm_get_constant(code, &ip));
		break;
	op_case(op_jmp_if_false)
		buf_push(":%04x", vm_get_constant(code, &ip));
		break;
	op_case(op_jmp_if_disabler)
		buf_push(":%04x", vm_get_constant(code, &ip));
		break;
	op_case(op_jmp_if_disabler_keep)
		buf_push(":%04x", vm_get_constant(code, &ip));
		break;
	op_case(op_jmp)
		buf_push(":%04x", vm_get_constant(code, &ip));
		break;
	op_case(op_typecheck)
		buf_push(":%s", obj_type_to_s(vm_get_constant(code, &ip)));
		break;

	op_case(op_az_branch)
		buf_push(":%d", vm_get_constant(code, &ip));
		buf_push(", obj:%d, %d", vm_get_constant(code, &ip), vm_get_constant(code, &ip));
		break;
	op_case(op_az_merge)
		break;
	case op_count: UNREACHABLE;
	}
	// clang-format on

#undef buf_push

	assert(ip - base_ip == OP_WIDTH(code[base_ip]));
	return buf;
}

void
vm_dis(struct workspace *wk)
{
	uint32_t w = 60;

	char loc_buf[256];
	for (uint32_t i = 0; i < wk->vm.code.len;) {
		uint8_t op = wk->vm.code.e[i];
		const char *dis = vm_dis_inst(wk, wk->vm.code.e, i);
		struct source_location loc;
		struct source *src;
		vm_lookup_inst_location(&wk->vm, i, &loc, &src);
		snprintf(loc_buf, sizeof(loc_buf), "%s:%3d:%02d", src ? src->label : 0, loc.off, loc.len);
		printf("%-*s%s\n", w, dis, loc_buf);

		/* if (src) { */
		/* 	list_line_range(src, loc, 0); */
		/* } */
		i += 1 + op_operands[op];
	}
}

/******************************************************************************
 * vm ops
 ******************************************************************************/

static void
vm_push_dummy(struct workspace *wk)
{
	// Used when an op encounters an error but still needs to put something on the stack
	object_stack_push(wk, make_typeinfo(wk, tc_any));
}

static void
vm_execute_capture(struct workspace *wk, obj a)
{
	uint32_t i;
	struct obj_capture *capture;

	if (wk->vm.in_analyzer && get_obj_type(wk, a) == obj_typeinfo) {
		vm_push_dummy(wk);
		typecheck(wk, 0, a, tc_capture);
		return;
	} else if (!typecheck(wk, 0, a, tc_capture)) {
		vm_push_dummy(wk);
		return;
	}

	capture = get_obj_capture(wk, a);

	stack_push(&wk->stack, wk->vm.saw_disabler, false);
	bool ok = pop_args(wk, capture->func->an, capture->func->akw);
	bool saw_disabler = wk->vm.saw_disabler;
	stack_pop(&wk->stack, wk->vm.saw_disabler);

	if (!ok) {
		if (saw_disabler) {
			object_stack_push(wk, disabler_id);
		} else {
			object_stack_push(wk, make_typeinfo(wk, capture->func->return_type));
		}
		return;
	}

	arr_push(&wk->vm.call_stack,
		&(struct call_frame){
			.type = call_frame_type_func,
			.return_ip = wk->vm.ip,
			.scope_stack = wk->vm.scope_stack,
			.expected_return_type = capture->func->return_type,
			.lang_mode = wk->vm.lang_mode,
		});

	wk->vm.lang_mode = capture->func->lang_mode;

	wk->vm.scope_stack = capture->scope_stack;
	wk->vm.behavior.push_local_scope(wk);

	for (i = 0; capture->func->an[i].type != ARG_TYPE_NULL; ++i) {
		wk->vm.behavior.assign_variable(wk,
			capture->func->an[i].name,
			capture->func->an[i].val,
			capture->func->an[i].node,
			assign_local);
	}

	for (i = 0; capture->func->akw[i].key; ++i) {
		obj val = 0;
		if (capture->func->akw[i].set) {
			val = capture->func->akw[i].val;
		} else if (capture->defargs) {
			const struct str s = WKSTR(capture->func->akw[i].key);
			obj_dict_index_strn(wk, capture->defargs, s.s, s.len, &val);
		}

		wk->vm.behavior.assign_variable(
			wk, capture->func->akw[i].key, val, capture->func->akw[i].node, assign_local);
	}

	wk->vm.ip = capture->func->entry;
	return;
}

#define binop_disabler_check(a, b)                  \
	if (a == disabler_id || b == disabler_id) { \
		object_stack_push(wk, disabler_id); \
		return;                             \
	}

#define unop_disabler_check(a)                      \
	if (a == disabler_id) {                     \
		object_stack_push(wk, disabler_id); \
		return;                             \
	}

#define vm_pop(__it) entry = object_stack_pop_entry(&wk->vm.stack), __it = entry->o, __it##_ip = entry->ip
#define vm_peek(__it, __i) entry = object_stack_peek_entry(&wk->vm.stack, __i), __it = entry->o, __it##_ip = entry->ip

static void
vm_op_constant(struct workspace *wk)
{
	obj a;
	a = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	object_stack_push(wk, a);
}

static void
vm_op_constant_list(struct workspace *wk)
{
	obj b;
	uint32_t i, len = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	make_obj(wk, &b, obj_array);
	for (i = 0; i < len; ++i) {
		obj_array_push(wk, b, object_stack_peek(&wk->vm.stack, len - i));
	}

	object_stack_discard(&wk->vm.stack, len);
	object_stack_push(wk, b);
}

static void
vm_op_constant_dict(struct workspace *wk)
{
	obj b;
	uint32_t i, len = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	make_obj(wk, &b, obj_dict);
	for (i = 0; i < len; ++i) {
		obj key = object_stack_peek(&wk->vm.stack, (len - i) * 2 - 1);
		if (wk->vm.in_analyzer && get_obj_type(wk, key) == obj_typeinfo) {
			object_stack_discard(&wk->vm.stack, len * 2);
			object_stack_push(wk, make_typeinfo(wk, tc_dict));
			return;
		}

		obj_dict_set(wk, b, key, object_stack_peek(&wk->vm.stack, (len - i) * 2));
	}

	object_stack_discard(&wk->vm.stack, len * 2);
	object_stack_push(wk, b);
}

static void
vm_op_constant_func(struct workspace *wk)
{
	obj a, c, defargs;
	struct obj_capture *capture;

	defargs = object_stack_pop(&wk->vm.stack);
	a = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	make_obj(wk, &c, obj_capture);
	capture = get_obj_capture(wk, c);

	capture->func = get_obj_func(wk, a);
	capture->scope_stack = wk->vm.behavior.scope_stack_dup(wk, wk->vm.scope_stack);
	capture->defargs = defargs;

	object_stack_push_ip(wk, c, capture->func->entry);
}

#define typecheck_operand(__o, __o_type, __expect_type, __expect_tc_type, __result_type) \
	if (__o_type == obj_typeinfo) {                                                  \
		if (!typecheck_typeinfo(wk, __o, __expect_tc_type)) {                    \
			goto type_err;                                                   \
		} else {                                                                 \
			res = make_typeinfo(wk, __result_type);                          \
			break;                                                           \
		}                                                                        \
	} else if (__expect_type != 0 && __o_type != __expect_type) {                    \
		goto type_err;                                                           \
	}

struct check_obj_typeinfo_map {
	type_tag expect, result;
};

static bool
typecheck_typeinfo_operands(struct workspace *wk,
	obj a,
	obj b,
	obj *res,
	struct check_obj_typeinfo_map map[obj_type_count])
{
	type_tag ot, tc, a_t = get_obj_typeinfo(wk, a)->type, result = 0;
	uint32_t matches = 0;
	for (ot = 1; ot <= tc_type_count; ++ot) {
		tc = obj_type_to_tc_type(ot);
		if ((a_t & tc) != tc) {
			continue;
		} else if (!map[ot].expect) {
			continue;
		}

		if (typecheck_custom(wk, 0, b, map[ot].expect, 0)) {
			result |= map[ot].result;
			++matches;
		}
	}

	if (!matches) {
		return false;
	}

	*res = make_typeinfo(wk, result);
	return true;
}

static void
vm_op_add(struct workspace *wk)
{
	obj a, b;
	b = object_stack_pop(&wk->vm.stack);
	a = object_stack_pop(&wk->vm.stack);
	binop_disabler_check(a, b);

	enum obj_type a_t = get_obj_type(wk, a), b_t = get_obj_type(wk, b);

	obj res = 0;

	switch (a_t) {
	case obj_number: {
		typecheck_operand(b, b_t, obj_number, tc_number, tc_number);

		make_obj(wk, &res, obj_number);
		set_obj_number(wk, res, get_obj_number(wk, a) + get_obj_number(wk, b));
		break;
	}
	case obj_string: {
		typecheck_operand(b, b_t, obj_string, tc_string, tc_string);

		res = str_join(wk, a, b);
		break;
	}
	case obj_array: {
		obj_array_dup(wk, a, &res);

		if (b_t == obj_array) {
			obj_array_extend(wk, res, b);
		} else {
			obj_array_push(wk, res, b);
		}
		break;
	}
	case obj_dict: {
		typecheck_operand(b, b_t, obj_dict, tc_dict, tc_dict);

		obj_dict_merge(wk, a, b, &res);
		break;
	}
	case obj_typeinfo: {
		struct check_obj_typeinfo_map map[obj_type_count] = {
			[obj_number] = { tc_number, tc_number },
			[obj_string] = { tc_string, tc_string },
			[obj_dict] = { tc_dict, tc_dict },
			[obj_array] = { tc_any, tc_array },
		};
		if (!typecheck_typeinfo_operands(wk, a, b, &res, map)) {
			goto type_err;
		}
		break;
	}
	default:
type_err:
		vm_error(wk, "+ not defined for %s and %s", obj_typestr(wk, a), obj_typestr(wk, b));
		vm_push_dummy(wk);
		return;
	}

	object_stack_push(wk, res);
}

static void
vm_op_add_store(struct workspace *wk)
{
	obj a, b;

	b = object_stack_pop(&wk->vm.stack);
	obj a_id = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	const struct str *id = get_str(wk, a_id);
	if (!wk->vm.behavior.get_variable(wk, id->s, &a)) {
		vm_error(wk, "undefined object %s", get_cstr(wk, a_id));
		vm_push_dummy(wk);
		return;
	}

	enum obj_type a_t = get_obj_type(wk, a), b_t = get_obj_type(wk, b);
	obj res;
	bool assign = false;

	switch (a_t) {
	case obj_number: {
		assign = true;
		typecheck_operand(b, b_t, obj_number, tc_number, tc_number);

		make_obj(wk, &res, obj_number);
		set_obj_number(wk, res, get_obj_number(wk, a) + get_obj_number(wk, b));
		break;
	}
	case obj_string: {
		assign = true;
		typecheck_operand(b, b_t, obj_string, tc_string, tc_string);

		// TODO: could use str_appn, but would have to dup on store
		res = str_join(wk, a, b);
		break;
	}
	case obj_array: {
		if (b_t == obj_array) {
			obj_array_extend(wk, a, b);
		} else {
			obj_array_push(wk, a, b);
		}
		res = a;
		break;
	}
	case obj_dict: {
		typecheck_operand(b, b_t, obj_dict, tc_dict, tc_dict);

		obj_dict_merge_nodup(wk, a, b);
		res = a;
		break;
	}
	case obj_typeinfo: {
		assign = true;
		struct check_obj_typeinfo_map map[obj_type_count] = {
			[obj_number] = { tc_number, tc_number },
			[obj_string] = { tc_string, tc_string },
			[obj_dict] = { tc_dict, tc_dict },
			[obj_array] = { tc_any, tc_array },
		};
		if (!typecheck_typeinfo_operands(wk, a, b, &res, map)) {
			goto type_err;
		}
		break;
	}
	default:
type_err:
		vm_error(wk, "+= not defined for %s and %s", obj_typestr(wk, a), obj_typestr(wk, b));
		vm_push_dummy(wk);
		return;
	}

	if (assign) {
		wk->vm.behavior.assign_variable(wk, id->s, res, 0, assign_reassign);
	}

	object_stack_push(wk, res);
}

#define vm_simple_integer_op_body(__op, __strop)                                                            \
	obj a, b;                                                                                           \
	b = object_stack_pop(&wk->vm.stack);                                                                \
	a = object_stack_pop(&wk->vm.stack);                                                                \
	binop_disabler_check(a, b);                                                                         \
                                                                                                            \
	enum obj_type a_t = get_obj_type(wk, a), b_t = get_obj_type(wk, b);                                 \
	obj res;                                                                                            \
                                                                                                            \
	switch (a_t) {                                                                                      \
	case obj_number: {                                                                                  \
		typecheck_operand(b, b_t, obj_number, tc_number, tc_number);                                \
                                                                                                            \
		make_obj(wk, &res, obj_number);                                                             \
		set_obj_number(wk, res, get_obj_number(wk, a) __op get_obj_number(wk, b));                  \
		break;                                                                                      \
	}                                                                                                   \
	case obj_typeinfo: {                                                                                \
		struct check_obj_typeinfo_map map[obj_type_count] = {                                       \
			[obj_number] = { tc_number, tc_number },                                            \
		};                                                                                          \
		if (!typecheck_typeinfo_operands(wk, a, b, &res, map)) {                                    \
			goto type_err;                                                                      \
		}                                                                                           \
		break;                                                                                      \
	}                                                                                                   \
	default:                                                                                            \
type_err:                                                                                                   \
		vm_error(wk, __strop " not defined for %s and %s", obj_typestr(wk, a), obj_typestr(wk, b)); \
		vm_push_dummy(wk);                                                                          \
		return;                                                                                     \
	}                                                                                                   \
                                                                                                            \
	object_stack_push(wk, res);

static void
vm_op_sub(struct workspace *wk)
{
	vm_simple_integer_op_body(-, "-");
}

static void
vm_op_mul(struct workspace *wk)
{
	vm_simple_integer_op_body(*, "*");
}

static void
vm_op_mod(struct workspace *wk)
{
	vm_simple_integer_op_body(%, "%%");
}

static void
vm_op_div(struct workspace *wk)
{
	obj a, b;
	b = object_stack_pop(&wk->vm.stack);
	a = object_stack_pop(&wk->vm.stack);
	binop_disabler_check(a, b);

	enum obj_type a_t = get_obj_type(wk, a), b_t = get_obj_type(wk, b);
	obj res = 0;

	switch (a_t) {
	case obj_number:
		typecheck_operand(b, b_t, obj_number, tc_number, tc_number);

		make_obj(wk, &res, obj_number);
		set_obj_number(wk, res, get_obj_number(wk, a) / get_obj_number(wk, b));
		break;
	case obj_string: {
		typecheck_operand(b, b_t, obj_string, tc_string, tc_string);

		const struct str *ss1 = get_str(wk, a), *ss2 = get_str(wk, b);

		if (str_has_null(ss1)) {
			vm_error(wk, "%o is an invalid path", a);
			vm_push_dummy(wk);
			return;
		} else if (str_has_null(ss2)) {
			vm_error(wk, "%o is an invalid path", b);
			vm_push_dummy(wk);
			return;
		}

		SBUF(buf);
		path_join(wk, &buf, ss1->s, ss2->s);
		res = sbuf_into_str(wk, &buf);
		break;
	}
	case obj_typeinfo: {
		struct check_obj_typeinfo_map map[obj_type_count] = {
			[obj_number] = { tc_number, tc_number },
			[obj_string] = { tc_string, tc_string },
		};
		if (!typecheck_typeinfo_operands(wk, a, b, &res, map)) {
			goto type_err;
		}
		break;
	}
	default:
type_err:
		vm_error(wk, "/ not defined for %s and %s", obj_typestr(wk, a), obj_typestr(wk, b));
		vm_push_dummy(wk);
		return;
	}

	object_stack_push(wk, res);
}

#define vm_simple_comparison_op_body(__op, __strop)                                                         \
	obj a, b;                                                                                           \
	b = object_stack_pop(&wk->vm.stack);                                                                \
	a = object_stack_pop(&wk->vm.stack);                                                                \
	binop_disabler_check(a, b);                                                                         \
                                                                                                            \
	enum obj_type a_t = get_obj_type(wk, a), b_t = get_obj_type(wk, b);                                 \
	obj res = 0;                                                                                        \
                                                                                                            \
	switch (a_t) {                                                                                      \
	case obj_number: {                                                                                  \
		typecheck_operand(b, b_t, obj_number, tc_number, tc_number);                                \
                                                                                                            \
		res = get_obj_number(wk, a) __op get_obj_number(wk, b) ? obj_bool_true : obj_bool_false;    \
		break;                                                                                      \
	}                                                                                                   \
	case obj_typeinfo: {                                                                                \
		struct check_obj_typeinfo_map map[obj_type_count] = {                                       \
			[obj_number] = { tc_number, tc_bool },                                              \
		};                                                                                          \
		if (!typecheck_typeinfo_operands(wk, a, b, &res, map)) {                                    \
			goto type_err;                                                                      \
		}                                                                                           \
		break;                                                                                      \
	}                                                                                                   \
	default:                                                                                            \
type_err:                                                                                                   \
		vm_error(wk, __strop " not defined for %s and %s", obj_typestr(wk, a), obj_typestr(wk, b)); \
		vm_push_dummy(wk);                                                                          \
		return;                                                                                     \
	}                                                                                                   \
                                                                                                            \
	object_stack_push(wk, res);

static void
vm_op_lt(struct workspace *wk)
{
	vm_simple_comparison_op_body(<, "<");
}

static void
vm_op_gt(struct workspace *wk)
{
	vm_simple_comparison_op_body(>, ">");
}

static void
vm_op_in(struct workspace *wk)
{
	obj a, b;

	b = object_stack_pop(&wk->vm.stack);
	a = object_stack_pop(&wk->vm.stack);
	binop_disabler_check(a, b);

	enum obj_type a_t = get_obj_type(wk, a), b_t = get_obj_type(wk, b);
	obj res = 0;

	switch (b_t) {
	case obj_array: {
		typecheck_operand(a, a_t, 0, tc_any, tc_bool);

		res = obj_array_in(wk, b, a) ? obj_bool_true : obj_bool_false;
		break;
	}
	case obj_dict:
		typecheck_operand(a, a_t, obj_string, tc_string, tc_bool);

		res = obj_dict_in(wk, b, a) ? obj_bool_true : obj_bool_false;
		break;
	case obj_string:
		typecheck_operand(a, a_t, obj_string, tc_string, tc_bool);

		const struct str *r = get_str(wk, b), *l = get_str(wk, a);
		res = str_contains(r, l) ? obj_bool_true : obj_bool_false;
		break;
	case obj_typeinfo: {
		struct check_obj_typeinfo_map map[obj_type_count] = {
			[obj_array] = { tc_any, tc_bool },
			[obj_dict] = { tc_string, tc_bool },
			[obj_string] = { tc_string, tc_bool },
		};
		if (!typecheck_typeinfo_operands(wk, b, a, &res, map)) {
			goto type_err;
		}
		break;
	}
	default: {
type_err:
		vm_error(wk, "'in' not supported for %s and %s", obj_typestr(wk, a), obj_typestr(wk, b));
		vm_push_dummy(wk);
		return;
	}
	}

	object_stack_push(wk, res);
}

static void
vm_op_eq(struct workspace *wk)
{
	obj a, b;

	b = object_stack_pop(&wk->vm.stack);
	a = object_stack_pop(&wk->vm.stack);
	binop_disabler_check(a, b);

	enum obj_type a_t = get_obj_type(wk, a), b_t = get_obj_type(wk, b);
	obj res = 0;

	if (a_t == obj_typeinfo || b_t == obj_typeinfo) {
		res = make_typeinfo(wk, tc_bool);
	} else {
		res = obj_equal(wk, a, b) ? obj_bool_true : obj_bool_false;
	}

	object_stack_push(wk, res);
}

static void
vm_op_not(struct workspace *wk)
{
	obj a;
	a = object_stack_pop(&wk->vm.stack);
	unop_disabler_check(a);

	enum obj_type a_t = get_obj_type(wk, a);
	obj res = 0;

	switch (a_t) {
	case obj_bool: {
		res = get_obj_bool(wk, a) ? obj_bool_false : obj_bool_true;
		break;
	}
	case obj_typeinfo: {
		if (!typecheck_typeinfo(wk, a, tc_bool)) {
			goto type_err;
		}

		res = make_typeinfo(wk, tc_bool);
		break;
	}
	default:
type_err:
		vm_error(wk, "'not' not supported for %s", obj_typestr(wk, a));
		object_stack_push(wk, make_typeinfo(wk, tc_bool));
		return;
	}

	object_stack_push(wk, res);
}

static void
vm_op_negate(struct workspace *wk)
{
	obj a;
	a = object_stack_pop(&wk->vm.stack);
	unop_disabler_check(a);

	enum obj_type a_t = get_obj_type(wk, a);
	obj res = 0;

	switch (a_t) {
	case obj_number: {
		make_obj(wk, &res, obj_number);
		set_obj_number(wk, res, get_obj_number(wk, a) * -1);
		break;
	}
	case obj_typeinfo: {
		if (!typecheck_typeinfo(wk, a, tc_number)) {
			goto type_err;
		}

		res = make_typeinfo(wk, tc_number);
		break;
	}
	default:
type_err:
		vm_error(wk, "unary - not supported for %s", obj_typestr(wk, a));
		object_stack_push(wk, make_typeinfo(wk, tc_number));
		return;
	}

	object_stack_push(wk, res);
}

static void
vm_op_stringify(struct workspace *wk)
{
	obj a;
	a = object_stack_pop(&wk->vm.stack);

	obj res = 0;

	if (get_obj_type(wk, a) == obj_typeinfo) {
		if (!typecheck_typeinfo(wk, a, tc_bool | tc_file | tc_number | tc_string)) {
			vm_error(wk, "unable to coerce %s to string", obj_typestr(wk, a));
		}

		res = make_typeinfo(wk, tc_string);
	} else if (!coerce_string(wk, wk->vm.ip - 1, a, &res)) {
		vm_push_dummy(wk);
		return;
	}

	object_stack_push(wk, res);
}

static void
vm_op_store(struct workspace *wk)
{
	struct obj_stack_entry *a_entry;
	obj a, b;
	a_entry = object_stack_pop_entry(&wk->vm.stack);
	a = a_entry->o;
	b = object_stack_peek(&wk->vm.stack, 1);

	switch (get_obj_type(wk, b)) {
	case obj_environment:
	case obj_configuration_data: {
		obj cloned;
		if (!obj_clone(wk, wk, b, &cloned)) {
			UNREACHABLE;
		}

		b = cloned;
		break;
	}
	case obj_dict: {
		obj dup;
		obj_dict_dup(wk, b, &dup);
		b = dup;
		break;
	}
	case obj_array: {
		obj dup;
		obj_array_dup(wk, b, &dup);
		b = dup;
	}
	default: break;
	}

	wk->vm.behavior.assign_variable(wk, get_str(wk, a)->s, b, a_entry->ip, assign_local);
	/* LO("%o <= %o\n", a, b); */
}

static void
vm_op_load(struct workspace *wk)
{
	obj a, b;
	a = object_stack_pop(&wk->vm.stack);

	// a could be a disabler if this is an inlined get_variable call
	if (a == disabler_id) {
		object_stack_push(wk, disabler_id);
		return;
	}

	if (!wk->vm.behavior.get_variable(wk, get_str(wk, a)->s, &b)) {
		vm_error(wk, "undefined object %s", get_cstr(wk, a));
		vm_push_dummy(wk);
		return;
	}

	/* LO("%o <= %o\n", b, a); */
	object_stack_push(wk, b);
}

static void
vm_op_try_load(struct workspace *wk)
{
	obj a, b, res;
	b = object_stack_pop(&wk->vm.stack);
	a = object_stack_pop(&wk->vm.stack);

	if (a == disabler_id) {
		object_stack_push(wk, disabler_id);
		return;
	}

	if (!wk->vm.behavior.get_variable(wk, get_str(wk, a)->s, &res)) {
		res = b;
	}

	object_stack_push(wk, res);
}

static void
vm_op_index(struct workspace *wk)
{
	struct obj_stack_entry *entry;
	uint32_t b_ip;
	obj a, b;

	int64_t i;

	obj res = 0;
	vm_pop(b);
	a = object_stack_pop(&wk->vm.stack);
	binop_disabler_check(a, b);

	enum obj_type a_t = get_obj_type(wk, a), b_t = get_obj_type(wk, b);

	switch (a_t) {
	case obj_array: {
		typecheck_operand(b, b_t, obj_number, tc_number, tc_any);

		i = get_obj_number(wk, b);

		if (!boundscheck(wk, b_ip, get_obj_array(wk, a)->len, &i)) {
			break;
		}

		obj_array_index(wk, a, i, &res);
		break;
	}
	case obj_dict: {
		typecheck_operand(b, b_t, obj_string, tc_string, tc_any);

		if (!obj_dict_index(wk, a, b, &res)) {
			vm_error_at(wk, b_ip, "key not in dictionary: %o", b);
			vm_push_dummy(wk);
			return;
		}
		break;
	}
	case obj_custom_target: {
		typecheck_operand(b, b_t, obj_number, tc_number, tc_file);

		i = get_obj_number(wk, b);

		struct obj_custom_target *tgt = get_obj_custom_target(wk, a);
		struct obj_array *arr = get_obj_array(wk, tgt->output);

		if (!boundscheck(wk, b_ip, arr->len, &i)) {
			break;
		}

		obj_array_index(wk, tgt->output, i, &res);
		break;
	}
	case obj_string: {
		typecheck_operand(b, b_t, obj_number, tc_number, tc_string);

		i = get_obj_number(wk, b);

		const struct str *s = get_str(wk, a);
		if (!boundscheck(wk, b_ip, s->len, &i)) {
			break;
		}

		res = make_strn(wk, &s->s[i], 1);
		break;
	}
	case obj_iterator: {
		typecheck_operand(b, b_t, obj_number, tc_number, tc_number);

		i = get_obj_number(wk, b);

		struct obj_iterator *iter = get_obj_iterator(wk, a);

		// It should be impossible to get a different type of iterator
		// outside of a loop.
		assert(iter->type == obj_iterator_type_range);

		double r = (double)iter->data.range.stop - (double)iter->data.range.start;
		uint32_t steps = (uint32_t)(r / (double)iter->data.range.step + 0.5);

		if (!boundscheck(wk, b_ip, steps, &i)) {
			break;
		}

		make_obj(wk, &res, obj_number);
		set_obj_number(wk, res, (i * iter->data.range.step) + iter->data.range.start);
		break;
	}
	case obj_typeinfo: {
		struct check_obj_typeinfo_map map[obj_type_count] = {
			[obj_array] = { tc_number, tc_any },
			[obj_dict] = { tc_string, tc_any },
			[obj_custom_target] = { tc_number, tc_file },
			[obj_string] = { tc_number, tc_string },
			[obj_iterator] = { tc_number, tc_number },
		};
		if (!typecheck_typeinfo_operands(wk, a, b, &res, map)) {
			goto type_err;
		}
		break;
	}
	default: {
type_err:
		vm_error_at(wk, b_ip, "[] unsupported for %s and %s", obj_typestr(wk, a), obj_typestr(wk, b));
		vm_push_dummy(wk);
		return;
	}
	}

	object_stack_push(wk, res);
}

static void
vm_op_call(struct workspace *wk)
{
	wk->vm.nargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	wk->vm.nkwargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	vm_execute_capture(wk, object_stack_pop(&wk->vm.stack));
}

static void
vm_op_call_method(struct workspace *wk)
{
	obj a, b, f = 0;
	uint32_t idx;

	b = object_stack_pop(&wk->vm.stack);
	a = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	wk->vm.nargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	wk->vm.nkwargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	if (!wk->vm.behavior.func_lookup(wk, b, get_str(wk, a)->s, &idx, &f)) {
		if (b == disabler_id) {
			object_stack_discard(&wk->vm.stack, wk->vm.nargs + wk->vm.nkwargs * 2);
			object_stack_push(wk, disabler_id);
			return;
		}
		vm_error(wk, "method %o not found on %#o", a, obj_type_to_typestr(wk, b));
		vm_push_dummy(wk);
		return;
	}

	if (f) {
		// step backwards 2 constants so that the nargs / nkwargs
		// constants are where op_call expects them
		wk->vm.ip -= 2 * 3;
		object_stack_push(wk, f);
		wk->vm.ops.ops[op_call](wk);
		return;
	} else {
		a = 0;

		if (native_funcs[idx].self_transform && get_obj_type(wk, b) != obj_typeinfo) {
			b = native_funcs[idx].self_transform(wk, b);
		}

		stack_push(&wk->stack, wk->vm.saw_disabler, false);

		bool ok;
		{
#ifdef TRACY_ENABLE
			TracyCZoneC(tctx_func, 0xff5000, true);
			char func_name[1024];
			snprintf(func_name,
				sizeof(func_name),
				"%s.%s",
				obj_type_to_s(get_obj_type(wk, b)),
				native_funcs[idx].name);
			TracyCZoneName(tctx_func, func_name, strlen(func_name));
#endif

			ok = wk->vm.behavior.native_func_dispatch(wk, idx, b, &a);

			TracyCZoneEnd(tctx_func);
		}

		bool saw_disabler = wk->vm.saw_disabler;

		stack_pop(&wk->stack, wk->vm.saw_disabler);

		if (!ok) {
			if (saw_disabler) {
				a = disabler_id;
			} else {
				vm_error(wk,
					"in method %s.%s",
					obj_type_to_s(get_obj_type(wk, b)),
					native_funcs[idx].name);
				vm_push_dummy(wk);
				return;
			}
		}
	}

	object_stack_push(wk, a);
}

static void
vm_op_call_native(struct workspace *wk)
{
	obj a, b;
	wk->vm.nargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	wk->vm.nkwargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	a = 0;
	b = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	stack_push(&wk->stack, wk->vm.saw_disabler, false);

	bool ok;
	{
#ifdef TRACY_ENABLE
		TracyCZoneC(tctx_func, 0xff5000, true);
		char func_name[1024];
		snprintf(func_name, sizeof(func_name), "%s", native_funcs[b].name);
		TracyCZoneName(tctx_func, func_name, strlen(func_name));
#endif

		ok = wk->vm.behavior.native_func_dispatch(wk, b, 0, &a);

		TracyCZoneEnd(tctx_func);
	}

	bool saw_disabler = wk->vm.saw_disabler;
	stack_pop(&wk->stack, wk->vm.saw_disabler);

	if (!ok) {
		if (saw_disabler) {
			a = disabler_id;
		} else {
			vm_error(wk, "in function %s", native_funcs[b].name);
			vm_push_dummy(wk);
			return;
		}
	}

	object_stack_push(wk, a);
}

static void
vm_op_iterator(struct workspace *wk)
{
	obj a, iter;
	uint32_t a_ip;
	struct obj_iterator *iterator;
	struct obj_stack_entry *entry;

	vm_pop(a);
	enum obj_type a_type = get_obj_type(wk, a);

	uint32_t args_to_unpack = vm_get_constant(wk->vm.code.e, &wk->vm.ip), expected_args_to_unpack = 0;

	switch (a_type) {
	case obj_array:
		expected_args_to_unpack = 1;
		if (expected_args_to_unpack != args_to_unpack) {
			goto args_to_unpack_mismatch_error;
		}

		make_obj(wk, &iter, obj_iterator);
		object_stack_push(wk, iter);
		iterator = get_obj_iterator(wk, iter);

		iterator->type = obj_iterator_type_array;
		iterator->data.array = get_obj_array(wk, a);
		if (!iterator->data.array->len) {
			// TODO: update this when we implement array_elem
			iterator->data.array = 0;
		}
		break;
	case obj_dict: {
		expected_args_to_unpack = 2;
		if (expected_args_to_unpack != args_to_unpack) {
			goto args_to_unpack_mismatch_error;
		}

		make_obj(wk, &iter, obj_iterator);
		object_stack_push(wk, iter);
		iterator = get_obj_iterator(wk, iter);

		struct obj_dict *d = get_obj_dict(wk, a);
		if (d->flags & obj_dict_flag_big) {
			iterator->type = obj_iterator_type_dict_big;
			iterator->data.dict_big.h = bucket_arr_get(&wk->vm.objects.dict_hashes, d->data);
		} else {
			iterator->type = obj_iterator_type_dict_small;
			iterator->data.dict_small = bucket_arr_get(&wk->vm.objects.dict_elems, d->data);
		}
		break;
	}
	case obj_iterator: {
		expected_args_to_unpack = 1;
		if (expected_args_to_unpack != args_to_unpack) {
			goto args_to_unpack_mismatch_error;
		}

		object_stack_push(wk, a);
		iterator = get_obj_iterator(wk, a);

		assert(iterator->type == obj_iterator_type_range);
		iterator->data.range.i = iterator->data.range.start;
		break;
	}
	case obj_typeinfo: {
		enum obj_type t;
		if (typecheck_custom(wk, 0, a, tc_dict, 0)) {
			expected_args_to_unpack = 2;
			t = obj_dict;
		} else if (typecheck_custom(wk, 0, a, tc_array, 0)) {
			expected_args_to_unpack = 1;
			t = obj_array;
		} else if (typecheck_custom(wk, 0, a, tc_iterator, 0)) {
			expected_args_to_unpack = 1;
			t = obj_iterator;
		} else {
			goto type_error;
		}

		if (expected_args_to_unpack != args_to_unpack) {
			goto args_to_unpack_mismatch_error;
		}

		make_obj(wk, &iter, obj_iterator);
		object_stack_push(wk, iter);
		iterator = get_obj_iterator(wk, iter);
		iterator->type = obj_iterator_type_typeinfo;
		iterator->data.typeinfo.type = t;
		break;
	}
	default: {
type_error:
		vm_error_at(wk, a_ip, "unable to iterate over object of type %#o", obj_type_to_typestr(wk, a));
		goto push_dummy_iterator;
		break;
	}
	}

	return;

args_to_unpack_mismatch_error:
	vm_error(wk,
		"%s args to unpack, expected %d for %s",
		args_to_unpack > expected_args_to_unpack ? "too many" : "not enough",
		expected_args_to_unpack,
		obj_typestr(wk, a));

push_dummy_iterator:
	make_obj(wk, &iter, obj_iterator);
	object_stack_push(wk, iter);
	iterator = get_obj_iterator(wk, iter);
	iterator->type = obj_iterator_type_typeinfo;
	iterator->data.typeinfo.type = args_to_unpack == 2 ? obj_dict : obj_array;
}

static void
vm_op_iterator_next(struct workspace *wk)
{
	obj key = 0, val;
	struct obj_iterator *iterator;
	uint32_t break_jmp = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	iterator = get_obj_iterator(wk, object_stack_peek(&wk->vm.stack, 1));

	switch (iterator->type) {
	case obj_iterator_type_array:
		if (!iterator->data.array) {
			val = 0;
		} else {
			val = iterator->data.array->val;
			iterator->data.array
				= iterator->data.array->have_next ? get_obj_array(wk, iterator->data.array->next) : 0;
		}
		break;
	case obj_iterator_type_range:
		if (iterator->data.range.i >= iterator->data.range.stop) {
			val = 0;
		} else {
			make_obj(wk, &val, obj_number);
			set_obj_number(wk, val, iterator->data.range.i);
			iterator->data.range.i += iterator->data.range.step;
		}
		break;
	case obj_iterator_type_dict_small:
		if (!iterator->data.dict_small) {
			val = 0;
		} else {
			key = iterator->data.dict_small->key;
			val = iterator->data.dict_small->val;
			if (iterator->data.dict_small->next) {
				iterator->data.dict_small
					= bucket_arr_get(&wk->vm.objects.dict_elems, iterator->data.dict_small->next);
			} else {
				iterator->data.dict_small = 0;
			}
		}
		break;
	case obj_iterator_type_dict_big:
		if (iterator->data.dict_big.i >= iterator->data.dict_big.h->keys.len) {
			val = 0;
		} else {
			void *k = arr_get(&iterator->data.dict_big.h->keys, iterator->data.dict_big.i);
			uint64_t *v = hash_get(iterator->data.dict_big.h, k);
			key = *v >> 32;
			val = *v & 0xffffffff;
			++iterator->data.dict_big.i;
		}
		break;
	case obj_iterator_type_typeinfo: {
		if (iterator->data.typeinfo.i) {
			val = 0;
			break;
		}
		++iterator->data.typeinfo.i;

		switch (iterator->data.typeinfo.type) {
		case obj_dict: {
			key = make_typeinfo(wk, tc_string);
			val = make_typeinfo(wk, tc_any);
			break;
		}
		case obj_array: {
			val = make_typeinfo(wk, tc_any);
			break;
		}
		case obj_iterator: {
			val = make_typeinfo(wk, tc_number);
			break;
		}
		default: UNREACHABLE;
		}
		break;
	}
	}

	if (!val) {
		wk->vm.ip = break_jmp;
		return;
	}

	object_stack_push(wk, val);
	if (key) {
		object_stack_push(wk, key);
	}
}

static void
vm_op_pop(struct workspace *wk)
{
	object_stack_pop(&wk->vm.stack);
}

static void
vm_op_dup(struct workspace *wk)
{
	obj a;
	a = object_stack_peek(&wk->vm.stack, 1);
	object_stack_push(wk, a);
}

static void
vm_op_swap(struct workspace *wk)
{
	obj a, b;
	a = object_stack_pop(&wk->vm.stack);
	b = object_stack_pop(&wk->vm.stack);
	object_stack_push(wk, a);
	object_stack_push(wk, b);
}

static void
vm_op_jmp_if_disabler(struct workspace *wk)
{
	obj a, b;
	a = object_stack_peek(&wk->vm.stack, 1);
	b = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	if (a == disabler_id) {
		object_stack_discard(&wk->vm.stack, 1);
		wk->vm.ip = b;
	}
}

static void
vm_op_jmp_if_disabler_keep(struct workspace *wk)
{
	obj a, b;
	a = object_stack_peek(&wk->vm.stack, 1);
	b = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	if (a == disabler_id) {
		wk->vm.ip = b;
	}
}

static void
vm_op_jmp_if_true(struct workspace *wk)
{
	struct obj_stack_entry *entry;
	uint32_t a_ip;
	obj a, b;

	vm_pop(a);
	if (!typecheck(wk, a_ip, a, obj_bool)) {
		return;
	}

	b = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	if (get_obj_bool(wk, a)) {
		wk->vm.ip = b;
	}
}

static void
vm_op_jmp_if_false(struct workspace *wk)
{
	struct obj_stack_entry *entry;
	uint32_t a_ip;
	obj a, b;

	vm_pop(a);
	if (!typecheck(wk, a_ip, a, obj_bool)) {
		return;
	}

	b = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	if (!get_obj_bool(wk, a)) {
		wk->vm.ip = b;
	}
}

static void
vm_op_jmp(struct workspace *wk)
{
	obj a;

	a = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	wk->vm.ip = a;
}

void
vm_op_return(struct workspace *wk)
{
	struct obj_stack_entry *entry;
	uint32_t a_ip;
	obj a;

	struct call_frame *frame = arr_pop(&wk->vm.call_stack);
	wk->vm.ip = frame->return_ip;

	switch (frame->type) {
	case call_frame_type_eval: {
		wk->vm.run = false;
		break;
	}
	case call_frame_type_func:
		wk->vm.behavior.pop_local_scope(wk);
		wk->vm.scope_stack = frame->scope_stack;
		wk->vm.lang_mode = frame->lang_mode;
		vm_peek(a, 1);
		typecheck_custom(wk, a_ip, a, frame->expected_return_type, "expected return type %s, got %s");
		break;
	}
}

static void
vm_op_typecheck(struct workspace *wk)
{
	struct obj_stack_entry *entry;

	entry = object_stack_peek_entry(&wk->vm.stack, 1);
	typecheck(wk, entry->ip, entry->o, vm_get_constant(wk->vm.code.e, &wk->vm.ip));
}

/******************************************************************************
 * vm_execute
 ******************************************************************************/

static void
vm_abort_handler(void *ctx)
{
	struct workspace *wk = ctx;
	vm_error(wk, "encountered unhandled error");
}

bool
vm_eval_capture(struct workspace *wk, obj c, const struct args_norm an[], const struct args_kw akw[], obj *res)
{
	wk->vm.nargs = 0;
	if (an) {
		for (wk->vm.nargs = 0; an[wk->vm.nargs].type != ARG_TYPE_NULL; ++wk->vm.nargs) {
			object_stack_push_ip(wk, an[wk->vm.nargs].val, an[wk->vm.nargs].node);
		}
	}

	wk->vm.nkwargs = 0;
	if (akw) {
		uint32_t i;
		for (i = 0; akw[i].key; ++i) {
			if (!akw[i].val) {
				continue;
			}

			object_stack_push_ip(wk, akw[i].val, akw[i].node);
			object_stack_push(wk, make_str(wk, akw[i].key));
			++wk->vm.nkwargs;
		}
	}

	uint32_t call_stack_base = wk->vm.call_stack.len;
	arr_push(&wk->vm.call_stack,
		&(struct call_frame){
			.type = call_frame_type_eval,
			.return_ip = wk->vm.ip,
		});

	// Set the vm ip to 0 where vm_compile_initial_code_segment has placed a return statement
	wk->vm.ip = 0;
	vm_execute_capture(wk, c);

	vm_execute(wk);
	assert(call_stack_base == wk->vm.call_stack.len);

	bool ok = !wk->vm.error;
	*res = ok ? object_stack_pop(&wk->vm.stack) : 0;

	wk->vm.error = false;
	return ok;
}

static void
vm_unwind_call_stack(struct workspace *wk)
{
	struct call_frame *frame;
	while (wk->vm.call_stack.len) {
		frame = arr_pop(&wk->vm.call_stack);

		switch (frame->type) {
		case call_frame_type_eval: {
			wk->vm.ip = frame->return_ip;
			return;
		}
		case call_frame_type_func: break;
		}

		if (frame->return_ip) {
			vm_error_at(wk, frame->return_ip, "in function");
		}
	}
}

obj
vm_execute(struct workspace *wk)
{
	uint32_t object_stack_base = wk->vm.stack.ba.len;

	platform_set_abort_handler(vm_abort_handler, wk);

	stack_push(&wk->stack, wk->vm.run, true);

	wk->vm.behavior.execute_loop(wk);

	stack_pop(&wk->stack, wk->vm.run);

	if (wk->vm.error) {
		vm_unwind_call_stack(wk);
		assert(wk->vm.stack.ba.len >= object_stack_base);
		object_stack_discard(&wk->vm.stack, wk->vm.stack.ba.len - object_stack_base);
		return 0;
	} else {
		return object_stack_pop(&wk->vm.stack);
	}
}

/******************************************************************************
 * vm behavior functions
 ******************************************************************************/

/* muon stores variable scopes as an array of dicts.
 *
 * Each element of the array is a block scope.  Currently the only way to enter
 * a new block is inside of a function.
 *
 * For example, take the below scope_stack:
 *
 *     [{'a': 1}, {'b': 2}, {'c': 3}]
 *
 * This could be generated by the following code:
 *
 *     a = 1
 *
 *     func f()
 *         b = 2
 *         func g()
 *             c = 3 # at this point, scope_stack looks like the above.
 *         endfunc
 *         g()
 *     endfunc
 *
 *     f()
 *
 * When looking up variables, scopes are checked from the end of the
 * scope_stack.
 */

struct vm_check_scope_ctx {
	const char *name;
	obj res, scope;
	bool found;
};

static enum iteration_result
vm_check_scope(struct workspace *wk, void *_ctx, obj scope)
{
	struct vm_check_scope_ctx *ctx = _ctx;
	if (obj_dict_index_str(wk, scope, ctx->name, &ctx->res)) {
		ctx->scope = scope;
		ctx->found = true;
	}
	return ir_cont;
}

static bool
vm_get_local_variable(struct workspace *wk, const char *name, obj *res, obj *scope)
{
	struct vm_check_scope_ctx ctx = { .name = name };
	obj_array_foreach(wk, wk->vm.scope_stack, &ctx, vm_check_scope);

	if (ctx.found) {
		*res = ctx.res;
		*scope = ctx.scope;
		return true;
	}

	return false;
}

static bool
vm_get_variable(struct workspace *wk, const char *name, obj *res)
{
	obj o, _scope;

	if (vm_get_local_variable(wk, name, &o, &_scope)) {
		*res = o;
		return true;
	} else {
		return false;
	}
}

static enum iteration_result
vm_scope_stack_dup_iter(struct workspace *wk, void *_ctx, obj v)
{
	obj *r = _ctx;
	obj scope;
	obj_dict_dup(wk, v, &scope);
	obj_array_push(wk, *r, scope);
	return ir_cont;
}

static obj
vm_scope_stack_dup(struct workspace *wk, obj scope_stack)
{
	obj r;
	make_obj(wk, &r, obj_array);

	obj_array_foreach(wk, scope_stack, &r, vm_scope_stack_dup_iter);
	return r;
}

static void
vm_push_local_scope(struct workspace *wk)
{
	obj scope;
	make_obj(wk, &scope, obj_dict);
	obj_array_push(wk, wk->vm.scope_stack, scope);
}

static void
vm_pop_local_scope(struct workspace *wk)
{
	obj_array_pop(wk, wk->vm.scope_stack);
}

static void
vm_unassign_variable(struct workspace *wk, const char *name)
{
	obj _, scope;
	if (!vm_get_local_variable(wk, name, &_, &scope)) {
		return;
	}

	obj_dict_del_str(wk, scope, name);
}

static void
vm_assign_variable(struct workspace *wk, const char *name, obj o, uint32_t ip, enum variable_assignment_mode mode)
{
	obj scope = 0;
	if (mode == assign_reassign) {
		obj _;
		if (!vm_get_local_variable(wk, name, &_, &scope)) {
			UNREACHABLE;
		}
	} else {
		scope = obj_array_get_tail(wk, wk->vm.scope_stack);
	}

	obj_dict_set(wk, scope, make_str(wk, name), o);

	if (wk->vm.dbg_state.watched && obj_array_in(wk, wk->vm.dbg_state.watched, make_str(wk, name))) {
		LOG_I("watched variable \"%s\" changed", name);
		repl(wk, true);
	}
}

static bool
vm_native_func_dispatch(struct workspace *wk, uint32_t func_idx, obj self, obj *res)
{
	return native_funcs[func_idx].func(wk, self, res);
}

static void
vm_execute_loop(struct workspace *wk)
{
	uint32_t cip;
	while (wk->vm.run) {
		if (log_should_print(log_debug)) {
			/* LL("%-50s", vm_dis_inst(wk, wk->vm.code.e, wk->vm.ip).text); */
			/* object_stack_print(wk, &wk->vm.stack); */
		}

		cip = wk->vm.ip;
		++wk->vm.ip;
		wk->vm.ops.ops[wk->vm.code.e[cip]](wk);
	}
}

/******************************************************************************
 * init / destroy
 ******************************************************************************/

void
vm_init_objects(struct workspace *wk)
{
	bucket_arr_init(&wk->vm.objects.chrs, 4096, 1);
	bucket_arr_init(&wk->vm.objects.objs, 1024, sizeof(struct obj_internal));
	bucket_arr_init(&wk->vm.objects.dict_elems, 1024, sizeof(struct obj_dict_elem));
	bucket_arr_init(&wk->vm.objects.dict_hashes, 16, sizeof(struct hash));

	const struct {
		uint32_t item_size;
		uint32_t bucket_size;
	} sizes[] = {
		[obj_number] = { sizeof(int64_t), 1024 },
		[obj_string] = { sizeof(struct str), 1024 },
		[obj_compiler] = { sizeof(struct obj_compiler), 4 },
		[obj_array] = { sizeof(struct obj_array), 2048 },
		[obj_dict] = { sizeof(struct obj_dict), 512 },
		[obj_build_target] = { sizeof(struct obj_build_target), 16 },
		[obj_custom_target] = { sizeof(struct obj_custom_target), 16 },
		[obj_subproject] = { sizeof(struct obj_subproject), 16 },
		[obj_dependency] = { sizeof(struct obj_dependency), 16 },
		[obj_external_program] = { sizeof(struct obj_external_program), 32 },
		[obj_python_installation] = { sizeof(struct obj_python_installation), 32 },
		[obj_run_result] = { sizeof(struct obj_run_result), 32 },
		[obj_configuration_data] = { sizeof(struct obj_configuration_data), 16 },
		[obj_test] = { sizeof(struct obj_test), 64 },
		[obj_module] = { sizeof(struct obj_module), 16 },
		[obj_install_target] = { sizeof(struct obj_install_target), 128 },
		[obj_environment] = { sizeof(struct obj_environment), 4 },
		[obj_include_directory] = { sizeof(struct obj_include_directory), 16 },
		[obj_option] = { sizeof(struct obj_option), 32 },
		[obj_generator] = { sizeof(struct obj_generator), 16 },
		[obj_generated_list] = { sizeof(struct obj_generated_list), 16 },
		[obj_alias_target] = { sizeof(struct obj_alias_target), 4 },
		[obj_both_libs] = { sizeof(struct obj_both_libs), 4 },
		[obj_typeinfo] = { sizeof(struct obj_typeinfo), 32 },
		[obj_func] = { sizeof(struct obj_func), 32 },
		[obj_capture] = { sizeof(struct obj_func), 64 },
		[obj_source_set] = { sizeof(struct obj_source_set), 4 },
		[obj_source_configuration] = { sizeof(struct obj_source_configuration), 4 },
		[obj_iterator] = { sizeof(struct obj_iterator), 32 },
	};

	uint32_t i;
	for (i = _obj_aos_start; i < obj_type_count; ++i) {
		bucket_arr_init(&wk->vm.objects.obj_aos[i - _obj_aos_start], sizes[i].bucket_size, sizes[i].item_size);
	}

	bucket_arr_pushn(&wk->vm.objects.dict_elems, 0, 0, 1); // reserve dict_elem 0 as a null element

	hash_init(&wk->vm.objects.obj_hash, 128, sizeof(obj));
	hash_init_str(&wk->vm.objects.str_hash, 128);

	/* default objects */

	obj id;
	make_obj(wk, &id, obj_null);
	assert(id == 0);
}

void
vm_init(struct workspace *wk)
{
	wk->vm = (struct vm){ 0 };

	/* core vm runtime */
	object_stack_init(&wk->vm.stack);
	arr_init(&wk->vm.call_stack, 64, sizeof(struct call_frame));
	arr_init(&wk->vm.code, 4 * 1024, 1);
	arr_init(&wk->vm.src, 64, sizeof(struct source));
	arr_init(&wk->vm.locations, 1024, sizeof(struct source_location_mapping));

	/* compiler state */
	arr_init(&wk->vm.compiler_state.node_stack, 4096, sizeof(struct node *));
	arr_init(&wk->vm.compiler_state.if_jmp_stack, 64, sizeof(uint32_t));
	arr_init(&wk->vm.compiler_state.loop_jmp_stack, 64, sizeof(uint32_t));
	bucket_arr_init(&wk->vm.compiler_state.nodes, 2048, sizeof(struct node));

	/* behavior pointers */
	wk->vm.behavior = (struct vm_behavior){
		.assign_variable = vm_assign_variable,
		.unassign_variable = vm_unassign_variable,
		.push_local_scope = vm_push_local_scope,
		.pop_local_scope = vm_pop_local_scope,
		.scope_stack_dup = vm_scope_stack_dup,
		.get_variable = vm_get_variable,
		.eval_project_file = eval_project_file,
		.native_func_dispatch = vm_native_func_dispatch,
		.pop_args = vm_pop_args,
		.func_lookup = func_lookup,
		.execute_loop = vm_execute_loop,
	};

	/* ops */
	wk->vm.ops = (struct vm_ops){ .ops = {
					      [op_constant] = vm_op_constant,
					      [op_constant_list] = vm_op_constant_list,
					      [op_constant_dict] = vm_op_constant_dict,
					      [op_constant_func] = vm_op_constant_func,
					      [op_add] = vm_op_add,
					      [op_sub] = vm_op_sub,
					      [op_mul] = vm_op_mul,
					      [op_div] = vm_op_div,
					      [op_mod] = vm_op_mod,
					      [op_not] = vm_op_not,
					      [op_eq] = vm_op_eq,
					      [op_in] = vm_op_in,
					      [op_gt] = vm_op_gt,
					      [op_lt] = vm_op_lt,
					      [op_negate] = vm_op_negate,
					      [op_stringify] = vm_op_stringify,
					      [op_store] = vm_op_store,
					      [op_add_store] = vm_op_add_store,
					      [op_try_load] = vm_op_try_load,
					      [op_load] = vm_op_load,
					      [op_return] = vm_op_return,
					      [op_return_end] = vm_op_return,
					      [op_call] = vm_op_call,
					      [op_call_method] = vm_op_call_method,
					      [op_call_native] = vm_op_call_native,
					      [op_index] = vm_op_index,
					      [op_iterator] = vm_op_iterator,
					      [op_iterator_next] = vm_op_iterator_next,
					      [op_jmp_if_false] = vm_op_jmp_if_false,
					      [op_jmp_if_true] = vm_op_jmp_if_true,
					      [op_jmp_if_disabler] = vm_op_jmp_if_disabler,
					      [op_jmp_if_disabler_keep] = vm_op_jmp_if_disabler_keep,
					      [op_jmp] = vm_op_jmp,
					      [op_pop] = vm_op_pop,
					      [op_dup] = vm_op_dup,
					      [op_swap] = vm_op_swap,
					      [op_typecheck] = vm_op_typecheck,
				      } };

	/* objects */
	vm_init_objects(wk);

	obj id;
	make_obj(wk, &id, obj_disabler);
	assert(id == disabler_id);

	make_obj(wk, &id, obj_bool);
	assert(id == obj_bool_true);
	set_obj_bool(wk, id, true);

	make_obj(wk, &id, obj_bool);
	assert(id == obj_bool_false);
	set_obj_bool(wk, id, false);

	/* func impl tables */
	build_func_impl_tables();

	/* default scope */
	make_obj(wk, &wk->vm.default_scope_stack, obj_array);
	obj scope;
	make_obj(wk, &scope, obj_dict);
	obj_array_push(wk, wk->vm.default_scope_stack, scope);

	make_obj(wk, &id, obj_meson);
	obj_dict_set(wk, scope, make_str(wk, "meson"), id);

	make_obj(wk, &id, obj_machine);
	set_obj_machine(wk, id, machine_kind_build);
	obj_dict_set(wk, scope, make_str(wk, "build_machine"), id);

	make_obj(wk, &id, obj_machine);
	set_obj_machine(wk, id, machine_kind_host);
	obj_dict_set(wk, scope, make_str(wk, "host_machine"), id);
	obj_dict_set(wk, scope, make_str(wk, "target_machine"), id);

	wk->vm.scope_stack = wk->vm.behavior.scope_stack_dup(wk, wk->vm.default_scope_stack);

	/* initial code segment */
	vm_compile_initial_code_segment(wk);
}

void
vm_destroy_objects(struct workspace *wk)
{
	uint32_t i;
	struct bucket_arr *ba = &wk->vm.objects.obj_aos[obj_string - _obj_aos_start];
	for (i = 0; i < ba->len; ++i) {
		struct str *s = bucket_arr_get(ba, i);
		if (s->flags & str_flag_big) {
			z_free((void *)s->s);
		}
	}

	for (i = _obj_aos_start; i < obj_type_count; ++i) {
		bucket_arr_destroy(&wk->vm.objects.obj_aos[i - _obj_aos_start]);
	}

	for (i = 0; i < wk->vm.objects.dict_hashes.len; ++i) {
		struct hash *h = bucket_arr_get(&wk->vm.objects.dict_hashes, i);
		hash_destroy(h);
	}

	bucket_arr_destroy(&wk->vm.objects.chrs);
	bucket_arr_destroy(&wk->vm.objects.objs);
	bucket_arr_destroy(&wk->vm.objects.dict_elems);
	bucket_arr_destroy(&wk->vm.objects.dict_hashes);

	hash_destroy(&wk->vm.objects.obj_hash);
	hash_destroy(&wk->vm.objects.str_hash);
}

void
vm_destroy(struct workspace *wk)
{
	vm_destroy_objects(wk);

	bucket_arr_destroy(&wk->vm.stack.ba);
	arr_destroy(&wk->vm.call_stack);
	arr_destroy(&wk->vm.code);
	for (uint32_t i = 0; i < wk->vm.src.len; ++i) {
		struct source *src = arr_get(&wk->vm.src, i);
		if (src->reopen_type == source_reopen_type_file) {
			fs_source_destroy(src);
		}
	}
	arr_destroy(&wk->vm.src);
	arr_destroy(&wk->vm.locations);

	arr_destroy(&wk->vm.compiler_state.node_stack);
	arr_destroy(&wk->vm.compiler_state.if_jmp_stack);
	arr_destroy(&wk->vm.compiler_state.loop_jmp_stack);
	bucket_arr_destroy(&wk->vm.compiler_state.nodes);
}
