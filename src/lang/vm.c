/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "buf_size.h"
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
#include "platform/assert.h"
#include "platform/path.h"
#include "tracy.h"

const uint32_t op_operands[op_count] = {
	[op_iterator] = 1,
	[op_iterator_next] = 1,
	[op_store] = 1,
	[op_constant] = 1,
	[op_constant_list] = 1,
	[op_constant_dict] = 1,
	[op_constant_func] = 1,
	[op_call] = 2,
	[op_member] = 1,
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
object_stack_alloc_page(struct arena *a, struct object_stack *s)
{
	bucket_arr_pushn(a, &s->ba, 0, 0, object_stack_page_size);
	s->ba.len -= object_stack_page_size;
	++s->bucket;
	s->page = (struct obj_stack_entry *)((struct bucket *)s->ba.buckets.e)[s->bucket].mem;
	((struct bucket *)s->ba.buckets.e)[s->bucket].len = object_stack_page_size;
	s->i = 0;
}

static void
object_stack_init(struct arena *a, struct object_stack *s)
{
	bucket_arr_init(a, &s->ba, object_stack_page_size, struct obj_stack_entry);
	s->page = (struct obj_stack_entry *)((struct bucket *)s->ba.buckets.e)[0].mem;
	((struct bucket *)s->ba.buckets.e)[0].len = object_stack_page_size;
}

static void
object_stack_push_ip(struct workspace *wk, obj o, uint32_t ip)
{
	if (wk->vm.stack.i >= object_stack_page_size) {
		object_stack_alloc_page(wk->a, &wk->vm.stack);
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
	s->bucket = (s->ba.len ? s->ba.len - 1 : 0) / s->ba.bucket_size;
	s->page = (struct obj_stack_entry *)((struct bucket *)s->ba.buckets.e)[s->bucket].mem;
	s->i = s->ba.len - (s->bucket * s->ba.bucket_size);
}

void
object_stack_print(struct workspace *wk, struct object_stack *s)
{
	for (int32_t i = s->ba.len - 1; i >= 0; --i) {
		struct obj_stack_entry *e = bucket_arr_get(&s->ba, i);
		obj_lprintf(wk, log_debug, "%o%s", e->o, i > 0 ? ", " : "");
	}
	log_plain(log_debug, "\n");
}

/******************************************************************************
 * vm errors
 ******************************************************************************/

static uint32_t
vm_lookup_source_location_mapping_idx(struct vm *vm, uint32_t ip)
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

	return i;
}

void
vm_lookup_inst_location_src_idx(struct vm *vm, uint32_t ip, struct source_location *loc, uint32_t *src_idx)
{
	struct source_location_mapping *locations = (struct source_location_mapping *)vm->locations.e;

	uint32_t i = vm_lookup_source_location_mapping_idx(vm, ip);
	*loc = locations[i].loc;
	*src_idx = locations[i].src_idx;
}

void
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
vm_inst_location(struct workspace *wk, uint32_t ip, struct vm_inst_location *res)
{
	struct source_location loc;
	struct source *src;
	vm_lookup_inst_location(&wk->vm, ip, &loc, &src);
	struct detailed_source_location dloc;
	get_detailed_source_location(src, loc, &dloc, (enum get_detailed_source_location_flag)0);

	res->file = src->label;
	res->line = dloc.line;
	res->col = dloc.col;
	res->embedded = src->type == source_type_embedded;
}

obj
vm_inst_location_str(struct workspace *wk, uint32_t ip)
{
	struct vm_inst_location loc;
	vm_inst_location(wk, ip, &loc);

	return make_strf(wk, "%s%s:%d:%d", loc.embedded ? "[embedded] " : "", loc.file, loc.line, loc.col);
}

static obj
vm_inst_location_obj(struct workspace *wk, uint32_t ip)
{
	struct vm_inst_location loc;
	vm_inst_location(wk, ip, &loc);

	obj res;
	res = make_obj(wk, obj_array);
	obj_array_push(wk, res, make_strf(wk, "%s%s", loc.embedded ? "[embedded] " : "", loc.file));
	obj_array_push(wk, res, make_number(wk, loc.line));
	obj_array_push(wk, res, make_number(wk, loc.col));
	return res;
}

obj
vm_callstack(struct workspace *wk)
{
	obj res;
	res = make_obj(wk, obj_array);

	obj_array_push(wk, res, vm_inst_location_obj(wk, wk->vm.ip - 1));

	int32_t i;
	struct call_frame *frame;
	for (i = wk->vm.call_stack.len - 1; i >= 0; --i) {
		frame = arr_get(&wk->vm.call_stack, i);

		if (frame->return_ip) {
			obj_array_push(wk, res, vm_inst_location_obj(wk, frame->return_ip - 1));
		}
	}

	return res;
}

static void
vm_trigger_error(struct workspace *wk)
{
	if (wk->vm.in_analyzer) {
		az_set_error();
	} else {
		wk->vm.error = true;
		wk->vm.run = false;
	}
}

static void
vm_diagnostic_v(struct workspace *wk,
	uint32_t ip,
	enum log_level lvl,
	enum error_message_flag flags,
	const char *fmt,
	va_list args)
{
	static char buf[1024];
	obj_vsnprintf(wk, buf, ARRAY_LEN(buf), fmt, args);

	if (!ip) {
		ip = wk->vm.ip - 1;
	} else if (ip == UINT32_MAX) {
		ip = 0;
	}

	struct source_location loc = { 0 };
	struct source *src = 0;
	if (ip) {
		vm_lookup_inst_location(&wk->vm, ip, &loc, &src);
	}

	error_message(wk, src, loc, lvl, flags, buf);

	if (lvl == log_error) {
		vm_trigger_error(wk);
	}
}

void
vm_diagnostic(struct workspace *wk, uint32_t ip, enum log_level lvl, enum error_message_flag flags, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, ip, lvl, flags, fmt, args);
	va_end(args);
}

void
vm_error_at(struct workspace *wk, uint32_t ip, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, ip, log_error, 0, fmt, args);
	va_end(args);
}

void
vm_warning(struct workspace *wk, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, 0, log_warn, 0, fmt, args);
	va_end(args);
}

void
vm_warning_at(struct workspace *wk, uint32_t ip, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, ip, log_warn, 0, fmt, args);
	va_end(args);
}

void
vm_deprecation_at(struct workspace *wk, uint32_t ip, const char *since, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	static char buf[1024];
	obj_vsnprintf(wk, buf, ARRAY_LEN(buf), fmt, args);
	va_end(args);

	vm_diagnostic(wk, 0, log_warn, 0, "deprecated since %s: %s", since, buf);
}

void
vm_error(struct workspace *wk, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vm_diagnostic_v(wk, 0, log_error, 0, fmt, args);
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

static void
vm_function_arg_type_error(struct workspace *wk, uint32_t ip, obj obj_id, type_tag type, const char *name)
{
	vm_error_at(wk,
		ip,
		"expected type %s, got %s for argument%s%s",
		typechecking_type_to_s(wk, type),
		get_cstr(wk, obj_type_to_typestr(wk, obj_id)),
		name ? " " : "",
		name ? name : "");
}

static bool
typecheck_and_mutate_function_arg(struct workspace *wk, uint32_t ip, obj *val, type_tag type, const char *name)
{
	bool listify = (type & TYPE_TAG_LISTIFY) == TYPE_TAG_LISTIFY;
	type &= ~TYPE_TAG_LISTIFY;

	enum obj_type t = get_obj_type(wk, *val);

	// If obj_file or tc_file is requested, and the argument is an array of
	// length 1, try to unpack it.
	if (!listify && (t == obj_array || t == obj_typeinfo)
		&& (type == obj_file || (flatten_type(wk, type) & tc_file) == tc_file)) {
		if (t == obj_array && get_obj_array(wk, *val)->len == 1) {
			obj i0;
			i0 = obj_array_index(wk, *val, 0);
			if (get_obj_type(wk, i0) == obj_file) {
				*val = i0;
			}
		} else if (t == obj_typeinfo && typecheck_typeinfo(wk, *val, tc_array)) {
			return true;
		}
	}

	if (listify) {
		obj v, arr;
		arr = make_obj(wk, obj_array);

		if (t == obj_array) {
			obj_array_flat_for_(wk, *val, v, flat_iter) {
				if (v == obj_disabler) {
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
				} else if (!typecheck_custom(wk, ip, v, type, 0)) {
					vm_function_arg_type_error(wk, ip, v, type, name);
					obj_array_flat_iter_end(wk, &flat_iter);
					return false;
				}

				obj_array_push(wk, arr, v);
			}
		} else if (t == obj_typeinfo && typecheck_typeinfo(wk, *val, tc_array)) {
			return true;
		} else {
			if (*val == obj_disabler) {
				wk->vm.saw_disabler = true;
			} else if (!typecheck_custom(wk, ip, *val, type, 0)) {
				vm_function_arg_type_error(wk, ip, *val, type, name);
				return false;
			}

			obj_array_push(wk, arr, *val);
		}

		*val = arr;
		return true;
	}

	if (!typecheck_custom(wk, ip, *val, type, 0)) {
		vm_function_arg_type_error(wk, ip, *val, type, name);
		return false;
	}
	return true;
}

static bool
handle_kwarg(struct workspace *wk, struct args_kw akw[], const char *kw, uint32_t kw_ip, obj v, uint32_t v_ip)
{
	uint32_t i;
	bool glob = false;

	for (i = 0; akw[i].key; ++i) {
		if (akw[i].type & TYPE_TAG_GLOB) {
			glob = true;
			break;
		} else if (strcmp(kw, akw[i].key) == 0) {
			break;
		}
	}

	if (!akw[i].key) {
		vm_error_at(wk, kw_ip, "unknown kwarg %s", kw);
		return false;
	} else if (akw[i].set && !glob) {
		vm_error_at(wk, kw_ip, "keyword argument '%s' set twice", kw);
		return false;
	}

	if (!typecheck_and_mutate_function_arg(wk, v_ip, &v, akw[i].type & ~TYPE_TAG_GLOB, akw[i].key)) {
		return false;
	}

	if (glob) {
		obj_dict_set(wk, akw[i].val, make_str(wk, kw), v);
		akw[i].node = v_ip;
		akw[i].set = true;
	} else {
		akw[i].val = v;
		akw[i].node = v_ip;
		akw[i].set = true;
	}
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

	if (akw) {
		for (i = 0; akw[i].key; ++i) {
			akw[i].set = false;
			akw[i].val = 0;

			if (akw[i].type & TYPE_TAG_GLOB) {
				akw[i].val = make_obj(wk, obj_dict);
				akw[i].set = true;
			}
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
			if (entry->o == obj_disabler) {
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
				wk->vm.saw_disabler |= v == obj_disabler;
			}
		} else {
			uint32_t kw_ip = entry->ip;
			entry = object_stack_pop_entry(&wk->vm.stack);
			++args_popped;
			if (!handle_kwarg(wk, akw, kw, kw_ip, entry->o, entry->ip)) {
				goto err;
			}
			wk->vm.saw_disabler |= entry->o == obj_disabler;
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
			an[i].val = make_obj(wk, obj_array);
			for (j = i; j < wk->vm.nargs; ++j) {
				entry = object_stack_peek_entry(&wk->vm.stack, wk->vm.nargs - argi);
				wk->vm.saw_disabler |= entry->o == obj_disabler;
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
			wk->vm.saw_disabler |= entry->o == obj_disabler;
			an[i].val = entry->o;
			an[i].node = entry->ip;
			an[i].set = true;
			++argi;
		}

		if (!typecheck_and_mutate_function_arg(wk, an[i].node, &an[i].val, type, 0)) {
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

uint32_t
vm_constant_host_to_bc(uint32_t n)
{
#if !defined(MUON_ENDIAN)
	union {
		int i;
		char c;
	} u = { 1 };
	return u.c ? n : bswap_32(n) >> 8;
#elif MUON_ENDIAN == 1
	return bswap_32(n) >> 8;
#elif MUON_ENDIAN == 0
	return n;
#endif
}

static uint32_t
vm_constant_bc_to_host(uint32_t n)
{
#if !defined(MUON_ENDIAN)
	union {
		int i;
		char c;
	} u = { 1 };
	return u.c ? n : bswap_32(n << 8);
#elif MUON_ENDIAN == 1
	return bswap_32(n << 8);
#elif MUON_ENDIAN == 0
	return n;
#endif
}

obj
vm_get_constant(uint8_t *code, uint32_t *ip)
{
	obj r = (code[*ip + 0] << 16) | (code[*ip + 1] << 8) | code[*ip + 2];
	r = vm_constant_bc_to_host(r);
	*ip += 3;
	return r;
}

/******************************************************************************
 * disassembler
 ******************************************************************************/

const char *
vm_op_to_s(uint8_t op)
{
#define op_case(__op) \
	case __op: return #__op;

	// clang-format off
	switch (op) {
	op_case(op_pop)
	op_case(op_dup)
	op_case(op_swap)
	op_case(op_stringify)
	op_case(op_index)
	op_case(op_add)
	op_case(op_sub)
	op_case(op_mul)
	op_case(op_div)
	op_case(op_mod)
	op_case(op_eq)
	op_case(op_in)
	op_case(op_gt)
	op_case(op_lt)
	op_case(op_not)
	op_case(op_negate)
	op_case(op_return)
	op_case(op_return_end)
	op_case(op_try_load)
	op_case(op_load)
	op_case(op_store)
	op_case(op_iterator)
	op_case(op_iterator_next)
	op_case(op_constant)
	op_case(op_constant_list)
	op_case(op_constant_dict)
	op_case(op_constant_func)
	op_case(op_call)
	op_case(op_member)
	op_case(op_call_native)
	op_case(op_jmp_if_true)
	op_case(op_jmp_if_false)
	op_case(op_jmp_if_disabler)
	op_case(op_jmp_if_disabler_keep)
	op_case(op_jmp)
	op_case(op_typecheck)
	op_case(op_az_branch)
	op_case(op_az_merge)
	op_case(op_az_noop)
	op_case(op_dbg_break)
	case op_count: UNREACHABLE;
	}
	// clang-format on
#undef op_case

	UNREACHABLE_RETURN;
}

const char *
vm_dis_inst(struct workspace *wk, uint8_t *code, uint32_t base_ip)
{
	uint32_t i = 0;
	static char buf[2048];
	buf[0] = 0;
#define buf_push(...) i += obj_snprintf(wk, &buf[i], sizeof(buf) - i, __VA_ARGS__);

	uint32_t ip = base_ip;
	buf_push("%04x ", ip);

	uint32_t op = code[ip], constants[3];
	{
		++ip;
		uint32_t j;
		for (j = 0; j < op_operands[op]; ++j) {
			constants[j] = vm_get_constant(code, &ip);
		}
	}

	buf_push("%s", vm_op_to_s(op));

	switch (op) {
	case op_pop: break;
	case op_dup: break;
	case op_swap: break;
	case op_stringify: break;
	case op_index: break;
	case op_add: break;
	case op_sub: break;
	case op_mul: break;
	case op_div: break;
	case op_mod: break;
	case op_eq: break;
	case op_in: break;
	case op_gt: break;
	case op_lt: break;
	case op_not: break;
	case op_negate: break;
	case op_return: break;
	case op_return_end: break;
	case op_try_load: break;
	case op_load: break;

	case op_store: {
		buf_push(":%04x:", constants[0]);
		if (constants[0] & op_store_flag_member) {
			buf_push("member");
		}
		if (constants[0] & op_store_flag_add_store) {
			buf_push("+=");
		}
		break;
	}
	case op_iterator: buf_push(":%d", constants[0]); break;
	case op_iterator_next: buf_push(":%04x", constants[0]); break;
	case op_constant: buf_push(":%o", constants[0]); break;
	case op_constant_list: buf_push(":len:%d", constants[0]); break;
	case op_constant_dict: buf_push(":len:%d", constants[0]); break;
	case op_constant_func: buf_push(":%d", constants[0]); break;
	case op_call: buf_push(":%d,%d", constants[0], constants[1]); break;
	case op_member: {
		uint32_t a;
		a = constants[0];
		buf_push(":%o", a);
		break;
	}
	case op_call_native:
		buf_push(":");
		buf_push("%d,%d,", constants[0], constants[1]);
		uint32_t id = constants[2];
		buf_push("%s", native_funcs[id].name);
		break;
	case op_jmp_if_true: buf_push(":%04x", constants[0]); break;
	case op_jmp_if_false: buf_push(":%04x", constants[0]); break;
	case op_jmp_if_disabler: buf_push(":%04x", constants[0]); break;
	case op_jmp_if_disabler_keep: buf_push(":%04x", constants[0]); break;
	case op_jmp: buf_push(":%04x", constants[0]); break;
	case op_typecheck: buf_push(":%s", obj_type_to_s(constants[0])); break;

	case op_az_branch:
		buf_push(":%d", constants[0]);
		buf_push(", obj:%d, %d", constants[1], constants[2]);
		break;
	case op_az_merge: break;
	case op_dbg_break: break;
	case op_count: UNREACHABLE;
	}

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
		struct detailed_source_location dloc = { 0 };
		if (src) {
			get_detailed_source_location(src, loc, &dloc, (enum get_detailed_source_location_flag)0);
		}
		snprintf(loc_buf,
			sizeof(loc_buf),
			"%s:%3d:%02d - %3d:%02d [%d,%d]",
			src ? src->label : 0,
			dloc.line,
			dloc.col,
			dloc.end_line,
			dloc.end_col,
			loc.off,
			loc.len);
		log_plain(log_info, "%-*s%s\n", w, dis, loc_buf);

		/* if (src) { */
		/* 	list_line_range(src, loc, 0); */
		/* } */
		i += OP_WIDTH(op);
	}
}

/******************************************************************************
 * vm ops
 ******************************************************************************/

void
vm_push_call_stack_frame(struct workspace *wk, struct call_frame *frame)
{
	arr_push(wk->a, &wk->vm.call_stack, frame);
}

struct call_frame *
vm_pop_call_stack_frame(struct workspace *wk)
{
	struct call_frame *frame = arr_pop(&wk->vm.call_stack);
	switch (frame->type) {
	case call_frame_type_eval: break;
	case call_frame_type_func:
		wk->vm.behavior.pop_local_scope(wk);
		wk->vm.scope_stack = frame->scope_stack;
		wk->vm.lang_mode = frame->lang_mode;
		break;
	}

	return frame;
}

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

	capture = get_obj_capture(wk, a);

	stack_push(&wk->stack, wk->vm.saw_disabler, false);
	bool ok = pop_args(wk, capture->func->an, capture->func->akw);
	bool saw_disabler = wk->vm.saw_disabler;
	stack_pop(&wk->stack, wk->vm.saw_disabler);

	if (!ok) {
		if (saw_disabler) {
			object_stack_push(wk, obj_disabler);
		} else {
			object_stack_push(wk, make_typeinfo(wk, flatten_type(wk, capture->func->return_type)));
		}
		return;
	}

	vm_push_call_stack_frame(wk,
		&(struct call_frame){
			.type = call_frame_type_func,
			.return_ip = wk->vm.ip,
			.scope_stack = wk->vm.scope_stack,
			.expected_return_type = capture->func->return_type,
			.lang_mode = wk->vm.lang_mode,
			.func = capture->func,
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
			const struct str s = STRL(capture->func->akw[i].key);
			obj_dict_index_strn(wk, capture->defargs, s.s, s.len, &val);
		}

		wk->vm.behavior.assign_variable(
			wk, capture->func->akw[i].key, val, capture->func->akw[i].node, assign_local);
	}

	wk->vm.ip = capture->func->entry;
	return;
}

static void
vm_execute_native(struct workspace *wk, uint32_t func_idx, obj self)
{
	obj res = 0;

	stack_push(&wk->stack, wk->vm.saw_disabler, false);

	bool ok;
	{
#ifdef TRACY_ENABLE
		TracyCZoneC(tctx_func, 0xff5000, true);
		char func_name[1024];
		snprintf(func_name, sizeof(func_name), "%s", native_funcs[func_idx].name);
		TracyCZoneName(tctx_func, func_name, strlen(func_name));
#endif

		ok = wk->vm.behavior.native_func_dispatch(wk, func_idx, self, &res);

		TracyCZoneEnd(tctx_func);
	}

	bool saw_disabler = wk->vm.saw_disabler;
	stack_pop(&wk->stack, wk->vm.saw_disabler);

	if (!ok) {
		if (saw_disabler) {
			res = obj_disabler;
		} else {
			if (native_funcs[func_idx].flags & func_impl_flag_throws_error) {
				vm_trigger_error(wk);
			} else {
				vm_error(wk, "in function '%s'", native_funcs[func_idx].name);
			}

			vm_push_dummy(wk);
			return;
		}
	}

	object_stack_push(wk, res);
}

#define binop_disabler_check(a, b)                    \
	if (a == obj_disabler || b == obj_disabler) { \
		object_stack_push(wk, obj_disabler);  \
		return;                               \
	}

#define unop_disabler_check(a)                       \
	if (a == obj_disabler) {                     \
		object_stack_push(wk, obj_disabler); \
		return;                              \
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
	b = make_obj(wk, obj_array);
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
	b = make_obj(wk, obj_dict);
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

	c = make_obj(wk, obj_capture);
	capture = get_obj_capture(wk, c);

	capture->func = get_obj_func(wk, a);
	capture->scope_stack = wk->vm.behavior.scope_stack_dup(wk, wk->vm.scope_stack);
	capture->defargs = defargs;

	object_stack_push(wk, c);
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

		res = make_obj(wk, obj_number);
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
		} else if (b_t == obj_typeinfo) {
			type_tag t = get_obj_typeinfo(wk, b)->type;
			t &= ~(tc_array | obj_typechecking_type_tag);
			// Only push b if it has a type other than list.  This is because
			// if we push b with type tc_list then it looks like we just made a
			// nested list which [] + [] is never supposed to do.
			if (t) {
				obj_array_push(wk, res, b);
			}
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
		res = make_obj(wk, obj_number);                                                             \
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

		res = make_obj(wk, obj_number);
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

		workspace_scratch_begin(wk);
		TSTR(buf);
		path_join(wk, &buf, ss1->s, ss2->s);
		res = tstr_into_str(wk, &buf);
		workspace_scratch_end(wk);
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

static bool
vm_string_looks_like_version(const struct str *s)
{
	uint32_t i = 0;

	for (; i < s->len && is_digit(s->s[i]); ++i) {
	}

	if (!i) {
		return false;
	}

	return i < s->len && s->s[i] == '.';
}

static void
vm_check_string_comparison_with_version(struct workspace *wk, obj a, enum obj_type a_t, obj b, enum obj_type b_t, const char *op)
{
	const struct str *s1 = &STR("str"), *s2 = &STR("str");
	if (a_t == obj_string) {
		s1 = get_str(wk, a);
	}
	if (b_t == obj_string) {
		s2 = get_str(wk, b);
	}

	if (vm_string_looks_like_version(s1) || vm_string_looks_like_version(s2)) {
		vm_warning(wk,
			"suspicious use of lexicographic comparison with version string -- "
			"did you mean '%s'.version_compare('%s%s')?",
			a_t == obj_string ? get_str(wk, a)->s : "str",
			op,
			b_t == obj_string ? get_str(wk, b)->s : "version");
	}
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
	if (wk->vm.in_analyzer && (a_t == obj_string || b_t == obj_string)) {                               \
		vm_check_string_comparison_with_version(wk, a, a_t, b, b_t, __strop); \
	}                                                                                                   \
                                                                                                            \
	switch (a_t) {                                                                                      \
	case obj_number: {                                                                                  \
		typecheck_operand(b, b_t, obj_number, tc_number, tc_bool);                                  \
                                                                                                            \
		res = get_obj_number(wk, a) __op get_obj_number(wk, b) ? obj_bool_true : obj_bool_false;    \
		break;                                                                                      \
	}                                                                                                   \
	case obj_string: {                                                                                  \
		typecheck_operand(b, b_t, obj_string, tc_string, tc_string);                                \
                                                                                                            \
		const struct str *s1 = get_str(wk, a), *s2 = get_str(wk, b);                                \
		res = str_cmp(s1, s2) __op 0 ? obj_bool_true : obj_bool_false;                              \
		break;                                                                                      \
	}                                                                                                   \
	case obj_typeinfo: {                                                                                \
		struct check_obj_typeinfo_map map[obj_type_count] = {                                       \
			[obj_number] = { tc_number, tc_bool },                                              \
			[obj_string] = { tc_string, tc_bool },                                              \
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

		if (a_t == obj_string) {
			check_str_enum(wk, a, a_t, b, b_t, check_str_enum_op_in);
		}
		break;
	}
	case obj_dict:
		typecheck_operand(a, a_t, obj_string, tc_string, tc_bool);

		res = obj_dict_in(wk, b, a) ? obj_bool_true : obj_bool_false;

		if (res == obj_bool_false) {
			check_str_enum(wk, a, a_t, b, b_t, check_str_enum_op_in);
		}
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
		if (a_t == obj_typeinfo ) {
			check_str_enum(wk, a, a_t, b, b_t, check_str_enum_op_eq);
		} else if (b_t == obj_typeinfo) {
			check_str_enum(wk, b, b_t, a, a_t, check_str_enum_op_eq);
		}

		res = make_typeinfo(wk, tc_bool);
	} else {
		res = obj_equal(wk, a, b) ? obj_bool_true : obj_bool_false;

		if (res == obj_bool_false) {
			if (a_t == obj_string) {
				check_str_enum(wk, a, a_t, b, b_t, check_str_enum_op_eq);
			} else if (b_t == obj_string) {
				check_str_enum(wk, b, b_t, a, a_t, check_str_enum_op_eq);
			}
		}
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
		res = make_obj(wk, obj_number);
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
		if (!typecheck_typeinfo(wk, a, tc_bool | tc_file | tc_number | tc_string | tc_feature_opt)) {
			vm_error(wk, "unable to coerce %s to string", obj_typestr(wk, a));
		}

		res = make_typeinfo(wk, tc_string);
	} else if (!coerce_string(wk, wk->vm.ip - 1, a, &res)) {
		vm_push_dummy(wk);
		return;
	}

	object_stack_push(wk, res);
}

static bool
vm_op_store_member_target(struct workspace *wk,
	uint32_t ip,
	obj target_container,
	obj id,
	enum op_store_flags flags,
	obj **member_target)
{
	obj res;
	enum obj_type target_container_type = get_obj_type(wk, target_container), id_type = get_obj_type(wk, id);

	switch (target_container_type) {
	case obj_array: {
		typecheck_operand(id, id_type, obj_number, tc_number, tc_any);

		int64_t i;
		i = get_obj_number(wk, id);

		if (!boundscheck(wk, ip, get_obj_array(wk, target_container)->len, &i)) {
			return false;
		}

		*member_target = obj_array_index_pointer(wk, target_container, i);
		return true;
	}
	case obj_dict: {
		typecheck_operand(id, id_type, obj_string, tc_string, tc_any);

		const struct str *s = get_str(wk, id);
		*member_target = obj_dict_index_strn_pointer(wk, target_container, s->s, s->len);

		if (!*member_target) {
			if (flags & op_store_flag_add_store) {
				vm_error_at(wk, ip, "member %o not found on %s", id, obj_typestr(wk, target_container));
				return false;
			}

			obj_dict_set(wk, target_container, id, 0);
			*member_target = obj_dict_index_strn_pointer(wk, target_container, s->s, s->len);
		}
		return true;
	}
	case obj_typeinfo: {
		struct check_obj_typeinfo_map map[obj_type_count] = {
			[obj_array] = { tc_number, tc_any },
			[obj_dict] = { tc_string, tc_any },
		};
		if (!typecheck_typeinfo_operands(wk, target_container, id, &res, map)) {
			goto type_err;
		}
		break;
	}
	default:
type_err:
		vm_error_at(
			wk, ip, "unable to index %s with %s", obj_typestr(wk, target_container), obj_typestr(wk, id));
		break;
	}

	// If we got here it means that the member expression contains a typeinfo or there was a type error.
	return false;
}

static void
vm_op_store(struct workspace *wk)
{
	enum op_store_flags flags = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	struct obj_stack_entry *id_entry;
	obj id, val, *member_target = 0;

	/* op store operands come in different order depending on the store type:
	 *   regular store:
	 *     <destination_id> <value>
	 *   member store
	 *     <value> <container> <destination_id>
	 */
	if (flags & op_store_flag_member) {
		val = object_stack_pop(&wk->vm.stack);
		obj target_container = object_stack_pop(&wk->vm.stack);
		id_entry = object_stack_pop_entry(&wk->vm.stack);
		id = id_entry->o;

		if (!vm_op_store_member_target(wk, id_entry->ip, target_container, id, flags, &member_target)) {
			object_stack_push(wk, val);
			return;
		}
	} else {
		id_entry = object_stack_pop_entry(&wk->vm.stack);
		id = id_entry->o;
		val = object_stack_pop(&wk->vm.stack);
	}

	if (get_obj_type(wk, id) == obj_typeinfo) {
		object_stack_push(wk, val);
		return;
	}

	if (flags & op_store_flag_add_store) {
		obj source;
		const struct str *id_str = 0;

		if (member_target) {
			source = *member_target;
		} else {
			id_str = get_str(wk, id);
			if (!wk->vm.behavior.get_variable(wk, id_str->s, &source)) {
				vm_error(wk, "undefined object %o", id);
				vm_push_dummy(wk);
				return;
			}
		}

		enum obj_type source_t = get_obj_type(wk, source), val_t = get_obj_type(wk, val);
		obj res;
		bool assign = false;

		if (wk->vm.in_analyzer) {
			// This is to ensure that array and dict mutations inside branches
			// cause the mutated object to be marked dirty when the branch
			// completes.
			assign = true;
		}

		switch (source_t) {
		case obj_number: {
			assign = true;
			typecheck_operand(val, val_t, obj_number, tc_number, tc_number);

			res = make_obj(wk, obj_number);
			set_obj_number(wk, res, get_obj_number(wk, source) + get_obj_number(wk, val));
			break;
		}
		case obj_string: {
			assign = true;
			typecheck_operand(val, val_t, obj_string, tc_string, tc_string);

			// TODO: could use str_appn, but would have to dup on store
			res = str_join(wk, source, val);
			break;
		}
		case obj_array: {
			if (val_t == obj_array) {
				obj_array_extend(wk, source, val);
			} else {
				obj_array_push(wk, source, val);
			}
			res = source;
			break;
		}
		case obj_dict: {
			typecheck_operand(val, val_t, obj_dict, tc_dict, tc_dict);

			obj_dict_merge_nodup(wk, source, val);
			res = source;
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
			if (!typecheck_typeinfo_operands(wk, source, val, &res, map)) {
				goto type_err;
			}
			break;
		}
		default:
type_err:
			vm_error(wk, "+= not defined for %s and %s", obj_typestr(wk, source), obj_typestr(wk, val));
			vm_push_dummy(wk);
			return;
		}

		if (assign) {
			if (member_target) {
				*member_target = res;
			} else {
				wk->vm.behavior.assign_variable(wk, id_str->s, res, wk->vm.ip - 1, assign_reassign);
			}
		}

		object_stack_push(wk, res);
	} else {
		switch (get_obj_type(wk, val)) {
		case obj_environment:
		case obj_configuration_data: {
			// TODO: these objects are just tiny wrappers over dict and array.  They could probably use the same logic as below.
			obj cloned;
			if (!obj_clone(wk, wk, val, &cloned)) {
				UNREACHABLE;
			}

			val = cloned;
			break;
		}
		case obj_dict: {
			// TODO: If we could detect if this was the initial storage of a dict literal to a var, then we wouldn't have to dup this.
			obj dup;
			obj_dict_dup_light(wk, val, &dup);
			val = dup;
			break;
		}
		case obj_array: {
			val = obj_array_dup_light(wk, val);
			break;
		}
		case obj_typeinfo: {
			obj dup = make_obj(wk, obj_typeinfo);
			*get_obj_typeinfo(wk, dup) = *get_obj_typeinfo(wk, val);
			val = dup;
			break;
		}
		case obj_capture: {
			struct obj_capture *c = get_obj_capture(wk, val);
			if (c->func && !c->func->name) {
				c->func->name = get_str(wk, id)->s;
			}
			break;
		}
		default: break;
		}

		if (member_target) {
			*member_target = val;
		} else {
			wk->vm.behavior.assign_variable(wk, get_str(wk, id)->s, val, id_entry->ip, assign_local);
		}

		object_stack_push(wk, val);
	}
}

static void
vm_op_load(struct workspace *wk)
{
	obj a, b;
	a = object_stack_pop(&wk->vm.stack);

	// a could be a disabler if this is an inlined get_variable call
	if (a == obj_disabler) {
		object_stack_push(wk, obj_disabler);
		return;
	} else if (get_obj_type(wk, a) == obj_typeinfo) {
		vm_push_dummy(wk);
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

	if (a == obj_disabler) {
		object_stack_push(wk, obj_disabler);
		return;
	} else if (get_obj_type(wk, a) == obj_typeinfo) {
		vm_push_dummy(wk);
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

		res = obj_array_index(wk, a, i);
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

		res = obj_array_index(wk, tgt->output, i);
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

		res = make_obj(wk, obj_number);
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
		vm_error_at(wk, b_ip, "unable to index %s with %s", obj_typestr(wk, a), obj_typestr(wk, b));
		vm_push_dummy(wk);
		return;
	}
	}

	object_stack_push(wk, res);
}

static void
vm_op_member(struct workspace *wk)
{
	obj id, self, f = 0;
	uint32_t idx;

	self = object_stack_pop(&wk->vm.stack);
	id = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	if (!wk->vm.behavior.func_lookup(wk, self, get_str(wk, id)->s, &idx, &f)) {
		if (self == obj_disabler) {
			object_stack_push(wk, obj_disabler);
			return;
		} else if (get_obj_type(wk, self) == obj_dict) {
			obj res;
			if (obj_dict_index(wk, self, id, &res)) {
				object_stack_push(wk, res);
				return;
			}
		} else if (typecheck_typeinfo(wk, self, tc_dict)) {
			vm_push_dummy(wk);
			return;
		} else if (wk->vm.in_analyzer && get_obj_type(wk, self) == obj_module
			   && !get_obj_module(wk, self)->found) {
			// Don't error on missing functions for not-found modules
			vm_push_dummy(wk);
			return;
		}

		vm_error(wk, "member %o not found on %#o", id, obj_type_to_typestr(wk, self));
		vm_push_dummy(wk);
		return;
	}

	obj res = make_obj(wk, obj_capture);
	struct obj_capture *c = get_obj_capture(wk, res);

	if (f) {
		if (get_obj_type(wk, f) == obj_typeinfo) {
			vm_push_dummy(wk);
			return;
		}

		*c = *get_obj_capture(wk, f);
	} else {
		c->native_func = idx;

		if (native_funcs[idx].self_transform && get_obj_type(wk, self) != obj_typeinfo) {
			self = native_funcs[idx].self_transform(wk, self);
		}
	}

	c->self = self;

	object_stack_push(wk, res);
}

static void
vm_op_call(struct workspace *wk)
{
	wk->vm.nargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	wk->vm.nkwargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	obj f = object_stack_pop(&wk->vm.stack);

	if (f == obj_disabler) {
		object_stack_discard(&wk->vm.stack, wk->vm.nargs + wk->vm.nkwargs * 2);
		object_stack_push(wk, obj_disabler);
		return;
	}

	if (wk->vm.in_analyzer && get_obj_type(wk, f) == obj_typeinfo) {
		object_stack_discard(&wk->vm.stack, wk->vm.nargs + wk->vm.nkwargs * 2);
		vm_push_dummy(wk);
		typecheck(wk, 0, f, tc_capture);
		return;
	} else if (!typecheck(wk, 0, f, tc_capture)) {
		object_stack_discard(&wk->vm.stack, wk->vm.nargs + wk->vm.nkwargs * 2);
		vm_push_dummy(wk);
		return;
	}

	struct obj_capture *c = get_obj_capture(wk, f);
	workspace_scratch_begin(wk);
	if (c->func) {
		vm_execute_capture(wk, f);
	} else {
		vm_execute_native(wk, c->native_func, c->self);
	}
	workspace_scratch_end(wk);
}

static void
vm_op_call_native(struct workspace *wk)
{
	wk->vm.nargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	wk->vm.nkwargs = vm_get_constant(wk->vm.code.e, &wk->vm.ip);

	uint32_t idx = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	workspace_scratch_begin(wk);
	vm_execute_native(wk, idx, 0);
	workspace_scratch_end(wk);
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

		iter = make_obj(wk, obj_iterator);
		object_stack_push(wk, iter);
		iterator = get_obj_iterator(wk, iter);

		iterator->type = obj_iterator_type_array;

		obj dup = obj_array_dup_light(wk, a);
		struct obj_array *arr = get_obj_array(wk, dup);
		iterator->data.array = arr->len ? bucket_arr_get(&wk->vm.objects.array_elems, arr->head) : 0;
		break;
	case obj_dict: {
		expected_args_to_unpack = 2;
		if (expected_args_to_unpack != args_to_unpack) {
			goto args_to_unpack_mismatch_error;
		}

		iter = make_obj(wk, obj_iterator);
		object_stack_push(wk, iter);
		iterator = get_obj_iterator(wk, iter);

		obj dup;
		obj_dict_dup_light(wk, a, &dup);
		struct obj_dict *d = get_obj_dict(wk, dup);
		if (d->flags & obj_dict_flag_big) {
			iterator->type = obj_iterator_type_dict_big;
			iterator->data.dict_big.h = bucket_arr_get(&wk->vm.objects.dict_hashes, d->data);
		} else {
			iterator->type = obj_iterator_type_dict_small;
			if (d->len) {
				iterator->data.dict_small = bucket_arr_get(&wk->vm.objects.dict_elems, d->data);
			}
		}
		break;
	}
	case obj_iterator: {
		expected_args_to_unpack = 1;
		if (expected_args_to_unpack != args_to_unpack) {
			goto args_to_unpack_mismatch_error;
		}

		iterator = get_obj_iterator(wk, a);
		assert(iterator->type == obj_iterator_type_range);

		object_stack_push(wk, a);
		iterator->data.range.i = iterator->data.range.start;
		break;
	}
	case obj_typeinfo: {
		enum obj_type t;
		if ((get_obj_typechecking_type(wk, a) & (tc_dict | tc_array)) == (tc_dict | tc_array)) {
			expected_args_to_unpack = args_to_unpack;
			t = args_to_unpack == 1 ? obj_array : obj_dict;
		} else if (typecheck_custom(wk, 0, a, tc_dict, 0)) {
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

		iter = make_obj(wk, obj_iterator);
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
	iter = make_obj(wk, obj_iterator);
	object_stack_push(wk, iter);
	iterator = get_obj_iterator(wk, iter);
	iterator->type = obj_iterator_type_typeinfo;
	iterator->data.typeinfo.type = args_to_unpack == 2 ? obj_dict : obj_array;
}

static void
vm_op_iterator_next(struct workspace *wk)
{
	bool push_key = false;
	obj key = 0, val = 0;
	struct obj_iterator *iterator;
	uint32_t break_jmp = vm_get_constant(wk->vm.code.e, &wk->vm.ip);
	iterator = get_obj_iterator(wk, object_stack_peek(&wk->vm.stack, 1));
	bool should_break = false;

	switch (iterator->type) {
	case obj_iterator_type_array:
		if (!iterator->data.array) {
			should_break = true;
		} else {
			val = iterator->data.array->val;
			iterator->data.array = iterator->data.array->next ? bucket_arr_get(
						       &wk->vm.objects.array_elems, iterator->data.array->next) :
									    0;
		}
		break;
	case obj_iterator_type_range:
		if (iterator->data.range.i >= iterator->data.range.stop) {
			should_break = true;
		} else {
			val = make_obj(wk, obj_number);
			set_obj_number(wk, val, iterator->data.range.i);
			iterator->data.range.i += iterator->data.range.step;
		}
		break;
	case obj_iterator_type_dict_small:
		if (!iterator->data.dict_small) {
			should_break = true;
		} else {
			push_key = true;
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
			should_break = true;
		} else {
			push_key = true;
			void *k = sl_get_(sl_cast(&iterator->data.dict_big.h->keys),
				iterator->data.dict_big.i,
				iterator->data.dict_big.h->key_size);
			union obj_dict_big_dict_value *uv
				= (union obj_dict_big_dict_value *)hash_get(iterator->data.dict_big.h, k);
			key = uv->val.key;
			val = uv->val.val;
			++iterator->data.dict_big.i;
		}
		break;
	case obj_iterator_type_typeinfo: {
		// Let it loop twice to catch all variables modified in this impure
		// loop.
		if (iterator->data.typeinfo.i > 1) {
			should_break = true;
			break;
		}
		++iterator->data.typeinfo.i;

		switch (iterator->data.typeinfo.type) {
		case obj_dict: {
			push_key = true;
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

	if (should_break) {
		wk->vm.ip = break_jmp;
		return;
	}

	object_stack_push(wk, val);
	if (push_key) {
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
	if (a == obj_disabler) {
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
	if (a == obj_disabler) {
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

	struct call_frame *frame = vm_pop_call_stack_frame(wk);
	wk->vm.ip = frame->return_ip;

	switch (frame->type) {
	case call_frame_type_eval: {
		wk->vm.run = false;
		break;
	}
	case call_frame_type_func:
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

static void
vm_op_dbg_break(struct workspace *wk)
{
	if (wk->vm.dbg_state.break_cb) {
		wk->vm.dbg_state.break_cb(wk);
	} else {
		repl(wk, true);
	}
}

/******************************************************************************
 * vm_execute
 ******************************************************************************/

bool
vm_eval_capture(struct workspace *wk, obj c, const struct args_norm an[], const struct args_kw akw[], obj *res)
{
	bool ok;

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
	vm_push_call_stack_frame(wk,
		&(struct call_frame){
			.type = call_frame_type_eval,
			.return_ip = wk->vm.ip,
		});

	// Set the vm ip to 0 where vm_compile_initial_code_segment has placed a return statement
	wk->vm.ip = 0;
	vm_execute_capture(wk, c);

	if (wk->vm.error) {
		object_stack_pop(&wk->vm.stack);
		vm_pop_call_stack_frame(wk);
		goto err;
	}

	vm_execute(wk);

err:
	assert(call_stack_base == wk->vm.call_stack.len);

	ok = !wk->vm.error;
	*res = ok ? object_stack_pop(&wk->vm.stack) : 0;

	wk->vm.error = false;
	return ok;
}

static void
vm_unwind_call_stack(struct workspace *wk)
{
	struct call_frame *frame;

	while (wk->vm.call_stack.len) {
		frame = vm_pop_call_stack_frame(wk);

		switch (frame->type) {
		case call_frame_type_eval: {
			error_message_flush_coalesced_message(wk);
			wk->vm.ip = frame->return_ip;
			// TODO: this is a little hacky?  We need to make sure that
			// execution can continue even if we emitted errors.
			wk->vm.run = true;
			return;
		}
		case call_frame_type_func: break;
		}

		const char *fmt = frame->func->name ? "in function '%s'" : "in %s";
		const char *fname = frame->func->name ? frame->func->name : "anonymous function";
		vm_diagnostic(wk,
			frame->return_ip - 1,
			log_error,
			error_message_flag_no_source | error_message_flag_coalesce,
			fmt,
			fname);
	}

	error_message_flush_coalesced_message(wk);
}

obj
vm_execute(struct workspace *wk)
{
	TracyCZoneAutoS;
	uint32_t object_stack_base = wk->vm.stack.ba.len;

	stack_push(&wk->stack, wk->vm.run, true);

	wk->vm.behavior.execute_loop(wk);

	stack_pop(&wk->stack, wk->vm.run);

	if (wk->vm.error) {
		vm_unwind_call_stack(wk);
		assert(wk->vm.stack.ba.len >= object_stack_base);
		object_stack_discard(&wk->vm.stack, wk->vm.stack.ba.len - object_stack_base);
		TracyCZoneAutoE;
		return 0;
	} else {
		TracyCZoneAutoE;
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

static bool
vm_get_local_variable(struct workspace *wk, const char *name, obj *res, obj *scope)
{
	bool found = false;
	obj s, idx;
	obj_array_for(wk, wk->vm.scope_stack, s) {
		if (obj_dict_index_str(wk, s, name, &idx)) {
			*res = idx;
			*scope = s;
			found = true;
		}
	}

	return found;
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

static obj
vm_scope_stack_dup(struct workspace *wk, obj scope_stack)
{
	obj r, v;
	r = make_obj(wk, obj_array);
	obj_array_for(wk, scope_stack, v) {
		obj scope;
		obj_dict_dup(wk, v, &scope);
		obj_array_push(wk, r, scope);
	}
	return r;
}

static void
vm_push_local_scope(struct workspace *wk)
{
	obj scope;
	scope = make_obj(wk, obj_dict);
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

union vm_breakpoint {
	struct {
		uint32_t line, col;
	} dat;
	int64_t i;
};

static void
vm_dgb_enable(struct workspace *wk)
{
	if (wk->vm.dbg_state.dbg) {
		return;
	}

	wk->vm.dbg_state.dbg = true;
	wk->vm.dbg_state.eval_trace = make_obj(wk, obj_array);
	wk->vm.dbg_state.root_eval_trace = wk->vm.dbg_state.eval_trace;
}

void
vm_dbg_push_breakpoint(struct workspace *wk, obj file, uint32_t line, uint32_t col)
{
	vm_dgb_enable(wk);

	union vm_breakpoint breakpoint = { .dat = { line, col } };
	if (!wk->vm.dbg_state.breakpoints) {
		wk->vm.dbg_state.breakpoints = make_obj(wk, obj_dict);
	}

	obj file_bp;
	if (!obj_dict_index(wk, wk->vm.dbg_state.breakpoints, file, &file_bp)) {
		file_bp = make_obj(wk, obj_array);
		obj_dict_set(wk, wk->vm.dbg_state.breakpoints, file, file_bp);
	}

	L("pushing breakpoint for %s:%d:%d", get_cstr(wk, file), line, col);

	obj_array_push(wk, file_bp, make_number(wk, breakpoint.i));
}

void
vm_dbg_unpack_breakpoint(struct workspace *wk, obj v, uint32_t *line, uint32_t *col)
{
	union vm_breakpoint breakpoint = { .i = get_obj_number(wk, v) };
	*line = breakpoint.dat.line;
	*col = breakpoint.dat.col;
}

bool
vm_dbg_push_breakpoint_str(struct workspace *wk, const char *bp)
{
	const char *sep = strchr(bp, ':');
	obj name;
	uint32_t line, col = 0;
	if (sep) {
		const char *sep_col = strchr(sep + 1, ':');
		struct str l = STRL(sep + 1), c = { 0 };

		if (sep_col) {
			c = STRL(sep_col + 1);
			l.len -= (c.len + 1);
		}

		int64_t i;
		if (!str_to_i(&l, &i, true)) {
			LOG_E("invalid line number: %.*s", l.len, l.s);
			return false;
		}
		line = i;

		if (c.s) {
			if (!str_to_i(&c, &i, true)) {
				LOG_E("invalid column: %.*s", c.len, c.s);
				return false;
			}
			col = i;
		}

		TSTR(p);
		path_make_absolute(wk, &p, get_cstr(wk, make_strn(wk, bp, sep - bp)));
		name = tstr_into_str(wk, &p);
	} else {
		name = make_str(wk, bp);
		line = 0;
	}

	vm_dbg_push_breakpoint(wk, name, line, col);
	return true;
}

static void
vm_check_break(struct workspace *wk, uint32_t ip)
{
	bool should_break = false;

	struct source *src = 0;

	if (wk->vm.dbg_state.stepping) {
		struct source_location loc = { 0 };
		vm_lookup_inst_location(&wk->vm, ip, &loc, &src);

		if (wk->vm.dbg_state.prev_source_location.off != loc.off) {
			wk->vm.dbg_state.prev_source_location = loc;
			should_break = true;
		}
	}

	if (!should_break && wk->vm.dbg_state.break_after) {
		should_break = wk->vm.dbg_state.icount >= wk->vm.dbg_state.break_after;
	}

	if (should_break) {
		if (wk->vm.dbg_state.break_cb) {
			wk->vm.dbg_state.break_cb(wk);
		} else {
			repl(wk, true);
		}
	}
}

static void
vm_execute_loop(struct workspace *wk)
{
	uint32_t cip;
	while (wk->vm.run) {
		// LL("%-50s", vm_dis_inst(wk, wk->vm.code.e, wk->vm.ip));
		// object_stack_print(wk, &wk->vm.stack);

		log_progress(wk, wk->vm.ip);

		vm_check_break(wk, wk->vm.ip);

		cip = wk->vm.ip;
		++wk->vm.ip;

		// TracyCZoneN(tctx, "op", true);
		// {
		// 	const char *op_name = vm_op_to_s(wk->vm.code.e[cip]);
		// 	TracyCZoneName(tctx, op_name, strlen(op_name));
		// }

		wk->vm.ops.ops[wk->vm.code.e[cip]](wk);

		// TracyCZoneEnd(tctx);

		++wk->vm.dbg_state.icount;
	}
}

/******************************************************************************
 * struct/type registration
 ******************************************************************************/

static obj
vm_struct_type_dict(struct workspace *wk, enum vm_struct_type base_t)
{
	switch (base_t) {
	case vm_struct_type_enum_: return wk->vm.types.enums;
	case vm_struct_type_struct_: return wk->vm.types.structs;
	default: UNREACHABLE_RETURN;
	}
}

enum vm_struct_type
vm_make_struct_type(struct workspace *wk, enum vm_struct_type base_t, const char *name)
{
	obj def;
	if (!obj_dict_index_str(wk, vm_struct_type_dict(wk, base_t), name, &def)) {
		error_unrecoverable("type %s is not registered", name);
	}

	return base_t | (def << vm_struct_type_shift);
}

static void
vm_types_init(struct workspace *wk)
{
	if (!wk->vm.types.structs) {
		wk->vm.types.structs = make_obj(wk, obj_dict);
		wk->vm.types.enums = make_obj(wk, obj_dict);
		wk->vm.types.docs = make_obj(wk, obj_dict);
		wk->vm.types.top_level_docs = make_obj(wk, obj_dict);
		wk->vm.types.str_enum_values = make_obj(wk, obj_dict);
		wk->vm.types.str_enums = make_obj(wk, obj_dict);
	}
}

static bool
vm_register_common(struct workspace *wk, obj dict, obj name)
{
	obj def;
	if (obj_dict_index(wk, dict, name, &def)) {
		return false;
	}

	def = make_obj(wk, obj_dict);
	obj_dict_set(wk, dict, name, def);
	return true;
}

bool
vm_enum_(struct workspace *wk, const char *name)
{
	vm_types_init(wk);
	obj n = make_str(wk, name);
	if (!vm_register_common(wk, wk->vm.types.enums, n)) {
		return false;
	}

	obj_dict_set(wk, wk->vm.types.str_enum_values, n, make_obj(wk, obj_array));
	return true;
}

void
vm_enum_value_(struct workspace *wk, const char *name, const char *member, uint32_t value)
{
	obj def;
	if (!obj_dict_index_str(wk, wk->vm.types.enums, name, &def)) {
		error_unrecoverable("enum %s is not registered", name);
	}

	obj str_enum_values;
	if (!obj_dict_index_str(wk, wk->vm.types.str_enum_values, name, &str_enum_values)) {
		LO("%o\n", wk->vm.types.str_enum_values);
		UNREACHABLE;
	}
	obj m = make_strn_enum(wk, member, strlen(member), str_enum_values);
	obj_array_push(wk, str_enum_values, m);
	obj_dict_set(wk, def, m, value);
}

obj
vm_enum_values_(struct workspace *wk, const char *name)
{
	obj str_enum_values;
	if (!obj_dict_index_str(wk, wk->vm.types.str_enum_values, name, &str_enum_values)) {
		UNREACHABLE;
	}
	return str_enum_values;
}

bool
vm_struct_(struct workspace *wk, const char *name)
{
	vm_types_init(wk);
	return vm_register_common(wk, wk->vm.types.structs, make_str(wk, name));
}

const char *
vm_enum_docs_def(struct workspace *wk, obj def)
{
	obj doc;
	if (obj_dict_geti(wk, wk->vm.types.docs, def, &doc)) {
		return get_str(wk, doc)->s;
	}

	obj arr = make_obj(wk, obj_array);
	obj k, v;
	obj_dict_for(wk, def, k, v) {
		obj_array_push(wk, arr, k);
	}

	obj_array_join(wk, false, arr, make_str(wk, "|"), &doc);

	obj_dict_seti(wk, wk->vm.types.docs, def, doc);
	return get_str(wk, doc)->s;
}

const char *
vm_struct_docs_def(struct workspace *wk, obj def)
{
	obj doc;
	if (obj_dict_geti(wk, wk->vm.types.docs, def, &doc)) {
		return get_str(wk, doc)->s;
	}

	obj arr = make_obj(wk, obj_array);

	obj k, v;
	obj_dict_for(wk, def, k, v) {
		obj t = obj_array_index(wk, v, 1);
		enum vm_struct_type type = t & vm_struct_type_mask;
		const char *type_str = 0;

		switch (type) {
		case vm_struct_type_struct_: type_str = vm_struct_docs_def(wk, t >> vm_struct_type_shift); break;
		case vm_struct_type_enum_: type_str = vm_enum_docs_def(wk, t >> vm_struct_type_shift); break;
		case vm_struct_type_bool: type_str = "bool"; break;
		case vm_struct_type_str: type_str = "str"; break;
		case vm_struct_type_obj: type_str = "any"; break;
		}

		assert(type_str);

		obj_array_push(wk, arr, make_strf(wk, "%s?: %s", get_str(wk, k)->s, type_str));
	}

	obj joined;
	obj_array_join(wk, false, arr, make_str(wk, ",\n\t"), &joined);

	TSTR(buf);
	tstr_pushf(wk, &buf, "{\n\t%s\n}", get_str(wk, joined)->s);

	doc = tstr_into_str(wk, &buf);
	obj_dict_seti(wk, wk->vm.types.docs, def, doc);
	return get_str(wk, doc)->s;
}

const char *
vm_struct_docs_(struct workspace *wk, const char *name, const char *fmt)
{
	vm_types_init(wk);

	obj doc;
	if (obj_dict_index_str(wk, wk->vm.types.top_level_docs, name, &doc)) {
		return get_str(wk, doc)->s;
	}

	obj def;
	if (!obj_dict_index_str(wk, wk->vm.types.structs, name, &def)) {
		error_unrecoverable("struct %s is not registered", name);
	}

	const char *doc_str = vm_struct_docs_def(wk, def);

	TSTR(buf);
	tstr_pushf(wk, &buf, fmt, doc_str);

	doc = tstr_into_str(wk, &buf);
	obj_dict_set(wk, wk->vm.types.top_level_docs, make_str(wk, name), doc);
	return get_str(wk, doc)->s;
}

void
vm_struct_member_(struct workspace *wk, const char *name, const char *member, uint32_t offset, enum vm_struct_type t)
{
	obj def;
	if (!obj_dict_index_str(wk, wk->vm.types.structs, name, &def)) {
		error_unrecoverable("struct %s is not registered", name);
	}

	obj member_def;
	member_def = make_obj(wk, obj_array);
	obj_array_push(wk, member_def, offset);
	obj_array_push(wk, member_def, t);

	obj_dict_set(wk, def, make_str(wk, member), member_def);
}

static bool
vm_obj_to_enum_def(struct workspace *wk, obj def, obj o, void *s)
{
	if (!typecheck_custom(wk, 0, o, tc_string, 0)) {
		vm_error(wk,
			"expected type %s for enum, got %s",
			typechecking_type_to_s(wk, tc_string),
			get_cstr(wk, obj_type_to_typestr(wk, o)));
		return false;
	}

	obj v;
	if (!obj_dict_index(wk, def, o, &v)) {
		vm_error(wk, "unknown enum value %s", get_cstr(wk, o));
		return false;
	}

	*(uint32_t *)s = v;
	return true;
}

bool
vm_obj_to_enum_(struct workspace *wk, const char *name, obj o, void *s)
{
	obj def;
	if (!obj_dict_index_str(wk, wk->vm.types.enums, name, &def)) {
		error_unrecoverable("enum %s is not registered", name);
	}

	return vm_obj_to_enum_def(wk, def, o, s);
}

obj
vm_enum_to_obj_(struct workspace *wk, const char *name, uint32_t value)
{
	obj def;
	if (!obj_dict_index_str(wk, wk->vm.types.enums, name, &def)) {
		error_unrecoverable("enum %s is not registered", name);
	}

	obj k, v;
	obj_dict_for(wk, def, k, v) {
		if (v == value) {
			return k;
		}
	}

	error_unrecoverable("enums %s value %d is not registered", name, value);
	return 0;
}

static bool
vm_obj_to_struct_def(struct workspace *wk, obj def, obj o, void *s)
{
	type_tag expected_type;
	obj k, v, member_def;
	obj_dict_for(wk, o, k, v) {
		if (!obj_dict_index(wk, def, k, &member_def)) {
			vm_error(wk, "unknown key %s", get_cstr(wk, k));
			return false;
		}

		obj offset, t;
		offset = obj_array_index(wk, member_def, 0);
		t = obj_array_index(wk, member_def, 1);
		char *dest = (char *)s + offset;

		enum vm_struct_type type = t & vm_struct_type_mask;

		switch (type) {
		case vm_struct_type_struct_:
			if (!vm_obj_to_struct_def(wk, t >> vm_struct_type_shift, v, dest)) {
				return false;
			}
			break;
		case vm_struct_type_enum_:
			if (!vm_obj_to_enum_def(wk, t >> vm_struct_type_shift, v, dest)) {
				return false;
			}
			break;
		case vm_struct_type_bool:
			expected_type = tc_bool;
			if (!typecheck_custom(wk, 0, v, expected_type, 0)) {
				goto type_err;
			}
			*(bool *)dest = get_obj_bool(wk, v);
			break;
		case vm_struct_type_str:
			expected_type = tc_string;
			if (!typecheck_custom(wk, 0, v, expected_type, 0)) {
				goto type_err;
			}
			*(const struct str **)dest = get_str(wk, v);
			break;
		case vm_struct_type_obj: *(obj *)dest = v; break;
		}
	}

	return true;

type_err:
	vm_error(wk,
		"expected type %s for member %s, got %s",
		typechecking_type_to_s(wk, expected_type),
		get_cstr(wk, k),
		get_cstr(wk, obj_type_to_typestr(wk, v)));
	return false;
}

bool
vm_obj_to_struct_(struct workspace *wk, const char *name, obj o, void *s)
{
	obj def;
	if (!obj_dict_index_str(wk, wk->vm.types.structs, name, &def)) {
		error_unrecoverable("struct %s is not registered", name);
	}

	return vm_obj_to_struct_def(wk, def, o, s);
}

/******************************************************************************
 * object reflection
 ******************************************************************************/

obj
vm_reflected_obj_fields(struct workspace *wk, enum obj_type t)
{
	return wk->vm.objects.reflected.objs[t];
}

const struct vm_reflected_field *
vm_reflected_obj_field(struct workspace *wk, obj val)
{
	return bucket_arr_get(&wk->vm.objects.reflected.fields, get_obj_number(wk, val));
}

static void
vm_reflect_obj_field_(struct workspace *wk, enum obj_type t, const struct vm_reflected_field *f)
{
	if (!wk->vm.objects.reflected.objs[t]) {
		wk->vm.objects.reflected.objs[t] = make_obj(wk, obj_array);
	}

	int64_t i = wk->vm.objects.reflected.fields.len;
	bucket_arr_push(wk->a, &wk->vm.objects.reflected.fields, f);
	obj n = make_obj(wk, obj_number);
	set_obj_number(wk, n, i);
	obj_array_push(wk, wk->vm.objects.reflected.objs[t], n);
}

#define vm_reflect_obj_singleton(__type)

#define vm_reflect_obj_field(__type, __field_type, __name)      \
	vm_reflect_obj_field_(wk,                               \
		__type,                                         \
		&(struct vm_reflected_field){ .name = #__name,  \
			.type = #__field_type,                  \
			.off = offsetof(struct __type, __name), \
			.size = sizeof(__field_type) })

// NOTE: for vm_reflect_obj_field_simple offset is 0 because get_obj_internal
// returns &o->val which is already offset to point at the value
#define vm_reflect_obj_field_simple(__type, __field_type)          \
	vm_reflect_obj_field_(wk,                                  \
		__type,                                            \
		&(struct vm_reflected_field){ .name = 0,           \
			.type = #__field_type,                     \
			.off = 0, \
			.size = sizeof(__field_type) })

#define vm_reflect_obj_field_for_each_toolchain_component(__type, __field_type, __name)   \
	vm_reflect_obj_field(__type, __field_type, __name[toolchain_component_compiler]); \
	vm_reflect_obj_field(__type, __field_type, __name[toolchain_component_linker]);   \
	vm_reflect_obj_field(__type, __field_type, __name[toolchain_component_archiver])

void
vm_reflect_objects(struct workspace *wk)
{
	// singleton objects
	vm_reflect_obj_singleton(obj_null);
	vm_reflect_obj_singleton(obj_disabler);
	vm_reflect_obj_singleton(obj_meson);
	vm_reflect_obj_singleton(obj_bool);

	// simple objects
	vm_reflect_obj_field_simple(obj_file, obj);
	vm_reflect_obj_field_simple(obj_feature_opt, enum feature_opt_state);
	vm_reflect_obj_field_simple(obj_machine, enum machine_kind);

	// obj_run_result
	vm_reflect_obj_field(obj_run_result, obj, out);
	vm_reflect_obj_field(obj_run_result, obj, err);
	vm_reflect_obj_field(obj_run_result, int32_t, status);
	vm_reflect_obj_field(obj_run_result, enum run_result_flags, flags);

	// obj_configuration_data
	vm_reflect_obj_field(obj_configuration_data, obj, dict);

	// obj_option
	vm_reflect_obj_field(obj_option, obj, name);
	vm_reflect_obj_field(obj_option, obj, val);
	vm_reflect_obj_field(obj_option, obj, choices);
	vm_reflect_obj_field(obj_option, obj, max);
	vm_reflect_obj_field(obj_option, obj, min);
	vm_reflect_obj_field(obj_option, obj, deprecated);
	vm_reflect_obj_field(obj_option, obj, description);
	vm_reflect_obj_field(obj_option, uint32_t, ip);
	vm_reflect_obj_field(obj_option, enum option_value_source, source);
	vm_reflect_obj_field(obj_option, enum build_option_type, type);
	vm_reflect_obj_field(obj_option, enum build_option_kind, kind);
	vm_reflect_obj_field(obj_option, bool, yield);
	vm_reflect_obj_field(obj_option, bool, builtin);

	// obj_environment
	vm_reflect_obj_field(obj_environment, obj, actions);

	// obj_install_target
	vm_reflect_obj_field(obj_install_target, obj, src);
	vm_reflect_obj_field(obj_install_target, obj, dest);
	vm_reflect_obj_field(obj_install_target, bool, has_perm);
	vm_reflect_obj_field(obj_install_target, uint32_t, perm);
	vm_reflect_obj_field(obj_install_target, obj, exclude_directories);
	vm_reflect_obj_field(obj_install_target, obj, exclude_files);
	vm_reflect_obj_field(obj_install_target, enum install_target_type, type);
	vm_reflect_obj_field(obj_install_target, bool, build_target);

	// obj_test
	vm_reflect_obj_field(obj_test, obj, name);
	vm_reflect_obj_field(obj_test, obj, exe);
	vm_reflect_obj_field(obj_test, obj, args);
	vm_reflect_obj_field(obj_test, obj, env);
	vm_reflect_obj_field(obj_test, obj, suites);
	vm_reflect_obj_field(obj_test, obj, workdir);
	vm_reflect_obj_field(obj_test, obj, depends);
	vm_reflect_obj_field(obj_test, obj, timeout);
	vm_reflect_obj_field(obj_test, obj, priority);
	vm_reflect_obj_field(obj_test, bool, should_fail);
	vm_reflect_obj_field(obj_test, bool, is_parallel);
	vm_reflect_obj_field(obj_test, bool, verbose);
	vm_reflect_obj_field(obj_test, enum test_category, category);
	vm_reflect_obj_field(obj_test, enum test_protocol, protocol);

	// obj_compiler
	vm_reflect_obj_field_for_each_toolchain_component(obj_compiler, obj, cmd_arr);
	vm_reflect_obj_field_for_each_toolchain_component(obj_compiler, obj, overrides);
	vm_reflect_obj_field_for_each_toolchain_component(obj_compiler, uint32_t, type);
	vm_reflect_obj_field_for_each_toolchain_component(obj_compiler, obj, ver);
	vm_reflect_obj_field(obj_compiler, obj, libdirs);
	vm_reflect_obj_field(obj_compiler, obj, fwdirs);
	vm_reflect_obj_field(obj_compiler, enum compiler_language, lang);
	vm_reflect_obj_field(obj_compiler, enum machine_kind, machine);
}

/******************************************************************************
 * init / destroy
 ******************************************************************************/

void
vm_init_objects(struct workspace *wk)
{
	bucket_arr_init(wk->a, &wk->vm.objects.chrs, 4096, char);
	bucket_arr_init(wk->a, &wk->vm.objects.objs, 1024, struct obj_internal);
	bucket_arr_init(wk->a, &wk->vm.objects.dict_elems, 1024, struct obj_dict_elem);
	bucket_arr_init(wk->a, &wk->vm.objects.dict_hashes, 16, struct hash);
	bucket_arr_init(wk->a, &wk->vm.objects.array_elems, 1024, struct obj_array_elem);
	bucket_arr_init(wk->a, &wk->vm.objects.reflected.fields, 128, struct vm_reflected_field);

#define P(__type) sizeof(__type), ar_alignof(__type)
	const struct {
		uint32_t item_size;
		uint32_t item_align;
		uint32_t bucket_size;
	} sizes[] = {
		[obj_number] = { P(int64_t), 1024 },
		[obj_string] = { P(struct str), 1024 },
		[obj_compiler] = { P(struct obj_compiler), 4 },
		[obj_array] = { P(struct obj_array), 2048 },
		[obj_dict] = { P(struct obj_dict), 512 },
		[obj_build_target] = { P(struct obj_build_target), 16 },
		[obj_custom_target] = { P(struct obj_custom_target), 16 },
		[obj_subproject] = { P(struct obj_subproject), 16 },
		[obj_dependency] = { P(struct obj_dependency), 16 },
		[obj_external_program] = { P(struct obj_external_program), 32 },
		[obj_python_installation] = { P(struct obj_python_installation), 32 },
		[obj_run_result] = { P(struct obj_run_result), 32 },
		[obj_configuration_data] = { P(struct obj_configuration_data), 16 },
		[obj_test] = { P(struct obj_test), 64 },
		[obj_module] = { P(struct obj_module), 16 },
		[obj_install_target] = { P(struct obj_install_target), 128 },
		[obj_environment] = { P(struct obj_environment), 4 },
		[obj_include_directory] = { P(struct obj_include_directory), 16 },
		[obj_option] = { P(struct obj_option), 32 },
		[obj_generator] = { P(struct obj_generator), 16 },
		[obj_generated_list] = { P(struct obj_generated_list), 16 },
		[obj_alias_target] = { P(struct obj_alias_target), 4 },
		[obj_both_libs] = { P(struct obj_both_libs), 4 },
		[obj_typeinfo] = { P(struct obj_typeinfo), 32 },
		[obj_func] = { P(struct obj_func), 32 },
		[obj_capture] = { P(struct obj_func), 64 },
		[obj_source_set] = { P(struct obj_source_set), 4 },
		[obj_source_configuration] = { P(struct obj_source_configuration), 4 },
		[obj_iterator] = { P(struct obj_iterator), 32 },
	};
#undef P

	uint32_t i;
	for (i = _obj_aos_start; i < obj_type_count; ++i) {
		bucket_arr_init_(wk->a,
			&wk->vm.objects.obj_aos[i - _obj_aos_start],
			sizes[i].bucket_size,
			sizes[i].item_size,
			sizes[i].item_align);
	}

	// reserve dict_elem 0 and array_elem as a null element
	bucket_arr_pushn(wk->a, &wk->vm.objects.dict_elems, 0, 0, 1);
	bucket_arr_pushn(wk->a, &wk->vm.objects.array_elems, 0, 0, 1);

	hash_init_str(wk->a, &wk->vm.objects.str_hash, 128);

	make_default_objects(wk);
}

void
vm_init(struct workspace *wk)
{
	wk->vm = (struct vm){ 0 };

	/* core vm runtime */
	object_stack_init(wk->a, &wk->vm.stack);
	arr_init(wk->a, &wk->vm.call_stack, 64, struct call_frame);
	arr_init(wk->a, &wk->vm.code, 4 * 1024, char);
	arr_init(wk->a, &wk->vm.src, 64, struct source);
	arr_init(wk->a, &wk->vm.locations, 1024, struct source_location_mapping);

	/* compiler state */
	arr_init(wk->a, &wk->vm.compiler_state.node_stack, 4096, struct node *);
	arr_init(wk->a, &wk->vm.compiler_state.if_jmp_stack, 64, uint32_t);
	arr_init(wk->a, &wk->vm.compiler_state.loop_jmp_stack, 64, uint32_t);
	bucket_arr_init(wk->a, &wk->vm.compiler_state.nodes, 2048, struct node);

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
					      [op_try_load] = vm_op_try_load,
					      [op_load] = vm_op_load,
					      [op_return] = vm_op_return,
					      [op_return_end] = vm_op_return,
					      [op_call] = vm_op_call,
					      [op_member] = vm_op_member,
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
					      [op_dbg_break] = vm_op_dbg_break,
				      } };

	/* objects */
	vm_init_objects(wk);
	vm_reflect_objects(wk);

	/* func impl tables */
	build_func_impl_tables(wk);

	/* default scope */
	wk->vm.default_scope_stack = make_obj(wk, obj_array);
	obj scope;
	scope = make_obj(wk, obj_dict);
	obj_array_push(wk, wk->vm.default_scope_stack, scope);

	obj_dict_set(wk, scope, make_str(wk, "meson"), obj_meson);

	obj id;
	id = make_obj(wk, obj_machine);
	set_obj_machine(wk, id, machine_kind_build);
	obj_dict_set(wk, scope, make_str(wk, "build_machine"), id);

	id = make_obj(wk, obj_machine);
	set_obj_machine(wk, id, machine_kind_host);
	obj_dict_set(wk, scope, make_str(wk, "host_machine"), id);
	obj_dict_set(wk, scope, make_str(wk, "target_machine"), id);

	/* module cache */
	wk->vm.modules = make_obj(wk, obj_dict);

	/* complex type cache */
	wk->vm.objects.complex_types = make_obj(wk, obj_dict);

	/* scope stack */
	wk->vm.scope_stack = wk->vm.behavior.scope_stack_dup(wk, wk->vm.default_scope_stack);

	/* initial code segment */
	vm_compile_initial_code_segment(wk);
}

void
vm_mem_stat(struct workspace *wk, struct vm_mem_stats *stats)
{
	*stats = (struct vm_mem_stats){ 0 };
	uint32_t i;
	for (i = 0; i < wk->vm.objects.objs.len; ++i) {
		const struct obj_internal *o = bucket_arr_get(&wk->vm.objects.objs, i);
		++stats->count[o->t];
		stats->bytes[o->t] += sizeof(struct obj_internal);
	}

	for (uint32_t i = _obj_aos_start; i < obj_type_count; ++i) {
	}
}

void
vm_mem_stat_print(struct workspace *wk, struct vm_mem_stats *stats)
{
	printf("vm memory usage:\n");
	uint32_t i;
	for (i = 0; i < obj_type_count; ++i) {
		printf("%s - %d\n", obj_type_to_s(i), stats->count[i]);
	}
}
