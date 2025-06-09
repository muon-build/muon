/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Simon Zeni <simon@bl4ckb0ne.ca>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "error.h"
#include "lang/object.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/mem.h"
#include "tracy.h"

static bool making_default_objects = false;

static void *
get_obj_internal(struct workspace *wk, obj id, enum obj_type type)
{
	struct obj_internal *o = bucket_arr_get(&wk->vm.objects.objs, id);
	if (o->t != type) {
		LOG_E("internal type error, expected %s but got %s", obj_type_to_s(type), obj_type_to_s(o->t));
		abort();
		return NULL;
	}

	switch (type) {
	case obj_null:
	case obj_disabler:
	case obj_meson:
	case obj_bool:
		if (!making_default_objects) {
			error_unrecoverable("tried to get singleton object of type %s", obj_type_to_s(type));
		}
		// fallthrough
	case obj_file:
	case obj_feature_opt:
	case obj_machine: {
		return &o->val;
		break;
	}

	case obj_number:
	case obj_string:
	case obj_array:
	case obj_dict:
	case obj_compiler:
	case obj_build_target:
	case obj_custom_target:
	case obj_subproject:
	case obj_dependency:
	case obj_external_program:
	case obj_python_installation:
	case obj_run_result:
	case obj_configuration_data:
	case obj_test:
	case obj_module:
	case obj_install_target:
	case obj_environment:
	case obj_include_directory:
	case obj_option:
	case obj_generator:
	case obj_generated_list:
	case obj_alias_target:
	case obj_both_libs:
	case obj_typeinfo:
	case obj_source_set:
	case obj_source_configuration:
	case obj_iterator:
	case obj_func:
	case obj_capture: return bucket_arr_get(&wk->vm.objects.obj_aos[o->t - _obj_aos_start], o->val);

	default: assert(false && "tried to get invalid object type"); return NULL;
	}
}

void
make_default_objects(struct workspace *wk)
{
	making_default_objects = true;

	obj id;
	id = make_obj(wk, obj_null);
	assert(id == 0);

	id = make_obj(wk, obj_disabler);
	assert(id == obj_disabler);

	id = make_obj(wk, obj_meson);
	assert(id == obj_meson);

	id = make_obj(wk, obj_bool);
	assert(id == obj_bool_true);
	*(bool *)get_obj_internal(wk, id, obj_bool) = true;

	id = make_obj(wk, obj_bool);
	assert(id == obj_bool_false);
	*(bool *)get_obj_internal(wk, id, obj_bool) = false;

	making_default_objects = false;
}

enum obj_type
get_obj_type(struct workspace *wk, obj id)
{
	struct obj_internal *o = bucket_arr_get(&wk->vm.objects.objs, id);
	return o->t;
}

bool
get_obj_bool(struct workspace *wk, obj o)
{
	if (o == obj_bool_true) {
		return true;
	} else if (o == obj_bool_false) {
		return false;
	} else {
		UNREACHABLE;
	}

	/* return *(bool *)get_obj_internal(wk, o, obj_bool); */
}

obj
make_obj_bool(struct workspace *wk, bool v)
{
	return v ? obj_bool_true : obj_bool_false;
}

obj
get_obj_bool_with_default(struct workspace *wk, obj o, bool def)
{
	return o ? get_obj_bool(wk, o) : def;
}

obj
make_number(struct workspace *wk, int64_t n)
{
	obj o;
	o = make_obj(wk, obj_number);
	set_obj_number(wk, o, n);
	return o;
}

int64_t
get_obj_number(struct workspace *wk, obj o)
{
	return *(int64_t *)get_obj_internal(wk, o, obj_number);
}

void
set_obj_number(struct workspace *wk, obj o, int64_t v)
{
	*(int64_t *)get_obj_internal(wk, o, obj_number) = v;
}

obj *
get_obj_file(struct workspace *wk, obj o)
{
	return get_obj_internal(wk, o, obj_file);
}

const char *
get_file_path(struct workspace *wk, obj o)
{
	return get_cstr(wk, *get_obj_file(wk, o));
}

const struct str *
get_str(struct workspace *wk, obj s)
{
	return get_obj_internal(wk, s, obj_string);
}

enum feature_opt_state
get_obj_feature_opt(struct workspace *wk, obj fo)
{
	enum feature_opt_state state;
	state = *(enum feature_opt_state *)get_obj_internal(wk, fo, obj_feature_opt);

	if (state == feature_opt_auto) {
		obj auto_features_opt;
		// NOTE: wk->global_opts won't be initialized if we are loading
		// a serial dump
		if (wk->global_opts && get_option(wk, NULL, &STR("auto_features"), &auto_features_opt)) {
			struct obj_option *opt = get_obj_option(wk, auto_features_opt);
			return *(enum feature_opt_state *)get_obj_internal(wk, opt->val, obj_feature_opt);
		} else {
			return state;
		}
	} else {
		return state;
	}
}

void
set_obj_feature_opt(struct workspace *wk, obj fo, enum feature_opt_state state)
{
	*(enum feature_opt_state *)get_obj_internal(wk, fo, obj_feature_opt) = state;
}

enum machine_kind
get_obj_machine(struct workspace *wk, obj o)
{
	return *(enum machine_kind *)get_obj_internal(wk, o, obj_machine);
}

void
set_obj_machine(struct workspace *wk, obj o, enum machine_kind kind)
{
	*(enum machine_kind *)get_obj_internal(wk, o, obj_machine) = kind;
}

#define OBJ_GETTER(type)                                     \
	struct type *get_##type(struct workspace *wk, obj o) \
	{                                                    \
		return get_obj_internal(wk, o, type);        \
	}

OBJ_GETTER(obj_array)
OBJ_GETTER(obj_dict)
OBJ_GETTER(obj_compiler)
OBJ_GETTER(obj_build_target)
OBJ_GETTER(obj_custom_target)
OBJ_GETTER(obj_subproject)
OBJ_GETTER(obj_dependency)
OBJ_GETTER(obj_external_program)
OBJ_GETTER(obj_python_installation)
OBJ_GETTER(obj_run_result)
OBJ_GETTER(obj_configuration_data)
OBJ_GETTER(obj_test)
OBJ_GETTER(obj_module)
OBJ_GETTER(obj_install_target)
OBJ_GETTER(obj_environment)
OBJ_GETTER(obj_include_directory)
OBJ_GETTER(obj_option)
OBJ_GETTER(obj_generator)
OBJ_GETTER(obj_generated_list)
OBJ_GETTER(obj_alias_target)
OBJ_GETTER(obj_both_libs)
OBJ_GETTER(obj_typeinfo)
OBJ_GETTER(obj_func)
OBJ_GETTER(obj_capture)
OBJ_GETTER(obj_source_set)
OBJ_GETTER(obj_source_configuration)
OBJ_GETTER(obj_iterator)

#undef OBJ_GETTER

obj
make_obj(struct workspace *wk, enum obj_type type)
{
	uint32_t val;
	obj res = wk->vm.objects.objs.len;

	switch (type) {
	case obj_null:
	case obj_disabler:
	case obj_meson:
	case obj_bool:
		if (!making_default_objects) {
			UNREACHABLE;
		}
		// fallthrough
	case obj_file:
	case obj_machine:
	case obj_feature_opt: val = 0; break;

	case obj_number:
	case obj_string:
	case obj_array:
	case obj_dict:
	case obj_compiler:
	case obj_build_target:
	case obj_custom_target:
	case obj_subproject:
	case obj_dependency:
	case obj_external_program:
	case obj_python_installation:
	case obj_run_result:
	case obj_configuration_data:
	case obj_test:
	case obj_module:
	case obj_install_target:
	case obj_environment:
	case obj_include_directory:
	case obj_option:
	case obj_generator:
	case obj_generated_list:
	case obj_alias_target:
	case obj_both_libs:
	case obj_source_set:
	case obj_source_configuration:
	case obj_iterator:
	case obj_typeinfo:
	case obj_func:
	case obj_capture: {
		struct bucket_arr *ba = &wk->vm.objects.obj_aos[type - _obj_aos_start];
		val = ba->len;
		bucket_arr_pushn(ba, NULL, 0, 1);
		break;
	}
	default: assert(false && "tried to make invalid object type");
	}

	bucket_arr_push(&wk->vm.objects.objs, &(struct obj_internal){ .t = type, .val = val });
#ifdef TRACY_ENABLE
	if (wk->tracy.is_master_workspace) {
		uint64_t mem = 0;
		mem += bucket_arr_size(&wk->vm.objects.objs);
		uint32_t i;
		for (i = 0; i < obj_type_count - _obj_aos_start; ++i) {
			mem += bucket_arr_size(&wk->vm.objects.obj_aos[i]);
		}
#define MB(b) ((double)b / 1048576.0)
		TracyCPlot("objects", wk->vm.objects.objs.len);
		TracyCPlot("object memory (mb)", MB(mem));
		TracyCPlot("string memory (mb)", MB(bucket_arr_size(&wk->vm.objects.chrs)));
#undef MB
	}
#endif

	return res;
}

void
obj_set_clear_mark(struct workspace *wk, struct obj_clear_mark *mk)
{
	wk->vm.objects.obj_clear_mark_set = true;
	mk->obji = wk->vm.objects.objs.len;

	bucket_arr_save(&wk->vm.objects.chrs, &mk->chrs);
	bucket_arr_save(&wk->vm.objects.objs, &mk->objs);
	uint32_t i;
	for (i = 0; i < obj_type_count - _obj_aos_start; ++i) {
		bucket_arr_save(&wk->vm.objects.obj_aos[i], &mk->obj_aos[i]);
	}
}

void
obj_clear(struct workspace *wk, const struct obj_clear_mark *mk)
{
	struct obj_internal *o;
	struct str *ss;
	uint32_t i;
	for (i = mk->obji; i < wk->vm.objects.objs.len; ++i) {
		o = bucket_arr_get(&wk->vm.objects.objs, i);
		if (o->t == obj_string) {
			ss = bucket_arr_get(&wk->vm.objects.obj_aos[obj_string - _obj_aos_start], o->val);

			if (ss->flags & str_flag_big) {
				z_free((void *)ss->s);
			}
		}
	}

	bucket_arr_restore(&wk->vm.objects.objs, &mk->objs);
	bucket_arr_restore(&wk->vm.objects.chrs, &mk->chrs);

	for (i = 0; i < obj_type_count - _obj_aos_start; ++i) {
		bucket_arr_restore(&wk->vm.objects.obj_aos[i], &mk->obj_aos[i]);
	}
}

static struct {
	enum obj_type t;
	const char *name;
} obj_names[obj_type_count] = {
	{ .t = obj_null, .name = "null" },
	{ .t = obj_compiler, .name = "compiler" },
	{ .t = obj_dependency, .name = "dep" },
	{ .t = obj_meson, .name = "meson" },
	{ .t = obj_string, .name = "str" },
	{ .t = obj_number, .name = "int" },
	{ .t = obj_array, .name = "list" },
	{ .t = obj_dict, .name = "dict" },
	{ .t = obj_bool, .name = "bool" },
	{ .t = obj_file, .name = "file" },
	{ .t = obj_build_target, .name = "build_tgt" },
	{ .t = obj_subproject, .name = "subproject" },
	{ .t = obj_machine, .name = "build_machine" },
	{ .t = obj_feature_opt, .name = "feature" },
	{ .t = obj_external_program, .name = "external_program" },
	{ .t = obj_python_installation, .name = "python_installation" },
	{ .t = obj_run_result, .name = "runresult" },
	{ .t = obj_configuration_data, .name = "cfg_data" },
	{ .t = obj_custom_target, .name = "custom_tgt" },
	{ .t = obj_test, .name = "test" },
	{ .t = obj_module, .name = "module" },
	{ .t = obj_install_target, .name = "install_tgt" },
	{ .t = obj_environment, .name = "env" },
	{ .t = obj_include_directory, .name = "inc" },
	{ .t = obj_option, .name = "option" },
	{ .t = obj_disabler, .name = "disabler" },
	{ .t = obj_generator, .name = "generator" },
	{ .t = obj_generated_list, .name = "generated_list" },
	{ .t = obj_alias_target, .name = "alias_tgt" },
	{ .t = obj_both_libs, .name = "both_libs" },
	{ .t = obj_typeinfo, .name = "typeinfo" },
	{ .t = obj_func, .name = "func_def" },
	{ .t = obj_capture, .name = "function" },
	{ .t = obj_source_set, .name = "source_set" },
	{ .t = obj_source_configuration, .name = "source_configuration" },
	{ .t = obj_iterator, .name = "iterator" },
};

const char *
obj_type_to_s(enum obj_type t)
{
	uint32_t i;
	for (i = 0; i < obj_type_count; ++i) {
		if (obj_names[i].t == t) {
			return obj_names[i].name;
		}
	}

	assert(false && "unreachable");
	return NULL;
}

bool
s_to_type_tag(const char *s, type_tag *t)
{
	uint32_t i;
	for (i = 0; i < obj_type_count; ++i) {
		if (strcmp(s, obj_names[i].name) == 0) {
			*t = obj_type_to_tc_type(obj_names[i].t);
			return true;
		}
	}

	struct {
		type_tag t;
		const char *name;
	} extra_types[] = {
		{ .t = tc_exe, .name = "exe" },
		{ .t = tc_any, .name = "any" },
		{ .t = TYPE_TAG_LISTIFY, .name = "listify" },
		{ .t = TYPE_TAG_GLOB, .name = "glob" },
	};

	for (i = 0; i < ARRAY_LEN(extra_types); ++i) {
		if (strcmp(s, extra_types[i].name) == 0) {
			*t = extra_types[i].t;
			return true;
		}
	}

	return false;
}

struct obj_equal_iter_ctx {
	obj other_container;
	uint32_t i;
};

static enum iteration_result
obj_equal_array_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_equal_iter_ctx *ctx = _ctx;
	obj r;

	r = obj_array_index(wk, ctx->other_container, ctx->i);

	if (!obj_equal(wk, val, r)) {
		return ir_err;
	}

	++ctx->i;
	return ir_cont;
}

static enum iteration_result
obj_equal_dict_iter(struct workspace *wk, void *_ctx, obj key, obj val)
{
	struct obj_equal_iter_ctx *ctx = _ctx;
	obj r;

	if (!obj_dict_index(wk, ctx->other_container, key, &r)) {
		return ir_err;
	} else if (!obj_equal(wk, val, r)) {
		return ir_err;
	}

	return ir_cont;
}

bool
obj_equal(struct workspace *wk, obj left, obj right)
{
	if (left == right) {
		return true;
	}

	enum obj_type t = get_obj_type(wk, left), right_t = get_obj_type(wk, right);

	// if we get <array> == <iterator> swap operands so we only have to
	// deal with the <iterator> == <array> case
	if (right_t == obj_iterator && t == obj_array) {
		obj s = left;
		enum obj_type st = t;
		left = right;
		t = right_t;
		right = s;
		right_t = st;
	}

	if (t == obj_iterator) {
		if (!(right_t == obj_array || right_t == obj_iterator)) {
			return false;
		}
	} else if (t != right_t) {
		return false;
	}

	switch (t) {
	case obj_string: return str_eql(get_str(wk, left), get_str(wk, right));
	case obj_file: return str_eql(get_str(wk, *get_obj_file(wk, left)), get_str(wk, *get_obj_file(wk, right)));
	case obj_number: return get_obj_number(wk, left) == get_obj_number(wk, right);
	case obj_bool: return get_obj_bool(wk, left) == get_obj_bool(wk, right);
	case obj_array: {
		struct obj_equal_iter_ctx ctx = {
			.other_container = right,
		};

		struct obj_array *l = get_obj_array(wk, left), *r = get_obj_array(wk, right);

		return l->len == r->len && obj_array_foreach(wk, left, &ctx, obj_equal_array_iter);
	}
	case obj_feature_opt: {
		return get_obj_feature_opt(wk, left) == get_obj_feature_opt(wk, right);
	}
	case obj_include_directory: {
		struct obj_include_directory *l, *r;
		l = get_obj_include_directory(wk, left);
		r = get_obj_include_directory(wk, right);

		return l->is_system == r->is_system && obj_equal(wk, l->path, r->path);
	}
	case obj_dict: {
		struct obj_equal_iter_ctx ctx = {
			.other_container = right,
		};

		struct obj_dict *l = get_obj_dict(wk, left), *r = get_obj_dict(wk, right);

		return l->len == r->len && obj_dict_foreach(wk, left, &ctx, obj_equal_dict_iter);
	}
	case obj_iterator: {
		struct obj_iterator *iter = get_obj_iterator(wk, left);
		assert(iter->type == obj_iterator_type_range);

		switch (right_t) {
		case obj_array: {
			int64_t a = iter->data.range.start, b;
			obj v;
			obj_array_for(wk, right, v) {
				if (a >= iter->data.range.step) {
					return false;
				}

				if (get_obj_type(wk, v) != obj_number) {
					return false;
				}
				b = get_obj_number(wk, v);
				if (a != b) {
					return false;
				}

				a += iter->data.range.step;
			}
			return true;
		}
		case obj_iterator: {
			struct obj_iterator *riter = get_obj_iterator(wk, right);
			assert(riter->type == obj_iterator_type_range);

			return iter->data.range.start == riter->data.range.start
			       && iter->data.range.stop == riter->data.range.stop
			       && iter->data.range.step == riter->data.range.step;
		}
		default: UNREACHABLE_RETURN;
		}

		break;
	}
	default:
		/* LOG_W("TODO: compare %s", obj_type_to_s(t)); */
		return false;
	}
}

/*******************************************************************************
 * arrays
 ******************************************************************************/

static void
obj_array_copy_on_write(struct workspace *wk, struct obj_array *a, obj arr)
{
	struct obj_array cur;

	if (!(a->flags & obj_array_flag_cow)) {
		return;
	}

	cur = *a;
	*a = (struct obj_array){ 0 };

	obj v;
	obj_array_for_array(wk, &cur, v) {
		obj_array_push(wk, arr, v);
	}
}

bool
obj_array_foreach(struct workspace *wk, obj arr, void *ctx, obj_array_iterator cb)
{
	obj v;
	obj_array_for(wk, arr, v) {
		switch (cb(wk, ctx, v)) {
		case ir_cont: break;
		case ir_done: return true;
		case ir_err: return false;
		}
	}

	return true;
}

bool
obj_array_foreach_flat(struct workspace *wk, obj arr, void *usr_ctx, obj_array_iterator cb)
{
	obj v;
	obj_array_flat_for_(wk, arr, v, iter) {
		switch (cb(wk, usr_ctx, v)) {
		case ir_cont: break;
		case ir_done: {
			obj_array_flat_iter_end(wk, &iter);
			return true;
		}
		case ir_err: {
			obj_array_flat_iter_end(wk, &iter);
			return false;
		}
		}
	}

	return true;
}

void
obj_array_push(struct workspace *wk, obj arr, obj child)
{
	struct obj_array_elem *e;
	struct obj_array *a;

	a = get_obj_array(wk, arr);
	obj_array_copy_on_write(wk, a, arr);

	uint32_t next = wk->vm.objects.array_elems.len;

	if (!a->len) {
		a->head = next;
	}

	bucket_arr_push(&wk->vm.objects.array_elems, &(struct obj_array_elem){ .val = child });

	if (a->len) {
		e = bucket_arr_get(&wk->vm.objects.array_elems, a->tail);
		e->next = next;
	}

	a->tail = next;
	++a->len;
}

void
obj_array_prepend(struct workspace *wk, obj *arr, obj val)
{
	obj prepend;
	prepend = make_obj(wk, obj_array);
	obj_array_push(wk, prepend, val);
	obj_array_extend_nodup(wk, prepend, *arr);
	*arr = prepend;
}

bool
obj_array_index_of(struct workspace *wk, obj arr, obj val, uint32_t *idx)
{
	obj v;
	uint32_t i = 0;
	obj_array_for(wk, arr, v) {
		if (obj_equal(wk, val, v)) {
			*idx = i;
			return true;
		}
		++i;
	}

	return false;
}

bool
obj_array_in(struct workspace *wk, obj arr, obj val)
{
	uint32_t _;
	return obj_array_index_of(wk, arr, val, &_);
}

static obj *
obj_array_index_pointer_raw(struct workspace *wk, obj arr, int64_t i)
{
	struct obj_array *a = get_obj_array(wk, arr);

	if (!a->len) {
		return 0;
	} else if (i == 0) {
		return &((struct obj_array_elem *)bucket_arr_get(&wk->vm.objects.array_elems, a->head))->val;
	} else if (i == a->len - 1) {
		return &((struct obj_array_elem *)bucket_arr_get(&wk->vm.objects.array_elems, a->tail))->val;
	}

	obj v;
	int64_t j = 0;
	obj_array_for_array_(wk, a, v, iter) {
		(void)v;
		if (j == i) {
			return &iter.e->val;
		}
		++j;
	}

	return 0;
}

obj *
obj_array_index_pointer(struct workspace *wk, obj arr, int64_t i)
{
	obj_array_copy_on_write(wk, get_obj_array(wk, arr), arr);
	return obj_array_index_pointer_raw(wk, arr, i);
}

obj
obj_array_index(struct workspace *wk, obj arr, int64_t i)
{
	obj *a = obj_array_index_pointer_raw(wk, arr, i);
	assert(a);
	return *a;
}

obj
obj_array_get_tail(struct workspace *wk, obj arr)
{
	uint32_t tail = get_obj_array(wk, arr)->tail;
	struct obj_array_elem *e = bucket_arr_get(&wk->vm.objects.array_elems, tail);
	return e->val;
}

obj
obj_array_get_head(struct workspace *wk, obj arr)
{
	uint32_t head = get_obj_array(wk, arr)->head;
	struct obj_array_elem *e = bucket_arr_get(&wk->vm.objects.array_elems, head);
	return e->val;
}

void
obj_array_dup(struct workspace *wk, obj arr, obj *res)
{
	*res = make_obj(wk, obj_array);
	obj v;
	obj_array_for(wk, arr, v) {
		obj_array_push(wk, *res, v);
	}
}

static void
obj_array_dup_into_light(struct workspace *wk, obj src, obj dst)
{
	struct obj_array *a_dst = get_obj_array(wk, dst), *a_src = get_obj_array(wk, src);
	*a_dst = *a_src;
	a_dst->flags |= obj_array_flag_cow;
	a_src->flags |= obj_array_flag_cow;
}

obj
obj_array_dup_light(struct workspace *wk, obj src)
{
	obj res;
	res = make_obj(wk, obj_array);
	obj_array_dup_into_light(wk, src, res);
	return res;
}

// mutates arr and consumes arr2
void
obj_array_extend_nodup(struct workspace *wk, obj arr, obj arr2)
{
	struct obj_array *a, *b;
	struct obj_array_elem *tail;

	if (!(b = get_obj_array(wk, arr2))->len) {
		return;
	}

	a = get_obj_array(wk, arr);
	obj_array_copy_on_write(wk, a, arr);

	if (!a->len) {
		obj_array_dup_into_light(wk, arr2, arr);
		return;
	}

	tail = bucket_arr_get(&wk->vm.objects.array_elems, a->tail);
	assert(!tail->next);
	tail->next = b->head;

	a->tail = b->tail;
	a->len += b->len;
}

// mutates arr without modifying arr2
void
obj_array_extend(struct workspace *wk, obj arr, obj arr2)
{
	obj dup;
	obj_array_dup(wk, arr2, &dup);
	obj_array_extend_nodup(wk, arr, dup);
}

struct obj_array_join_ctx {
	obj *res;
	const struct str *join;
	uint32_t i, len;
};

static enum iteration_result
obj_array_join_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_array_join_ctx *ctx = _ctx;

	if (!typecheck_simple_err(wk, val, obj_string)) {
		return ir_err;
	}

	const struct str *ss = get_str(wk, val);

	str_appn(wk, ctx->res, ss->s, ss->len);

	if (ctx->i < ctx->len - 1) {
		str_appn(wk, ctx->res, ctx->join->s, ctx->join->len);
	}

	++ctx->i;

	return ir_cont;
}

static enum iteration_result
obj_array_flat_len_iter(struct workspace *wk, void *_ctx, obj _)
{
	uint32_t *len = _ctx;
	++(*len);
	return ir_cont;
}

static uint32_t
obj_array_flat_len(struct workspace *wk, obj arr)
{
	uint32_t len = 0;
	obj_array_foreach_flat(wk, arr, &len, obj_array_flat_len_iter);
	return len;
}

bool
obj_array_join(struct workspace *wk, bool flat, obj arr, obj join, obj *res)
{
	*res = make_str(wk, "");

	if (!typecheck_simple_err(wk, join, obj_string)) {
		return false;
	}

	struct obj_array_join_ctx ctx = {
		.join = get_str(wk, join),
		.res = res,
	};

	if (flat) {
		ctx.len = obj_array_flat_len(wk, arr);
		return obj_array_foreach_flat(wk, arr, &ctx, obj_array_join_iter);
	} else {
		ctx.len = get_obj_array(wk, arr)->len;
		return obj_array_foreach(wk, arr, &ctx, obj_array_join_iter);
	}
}

void
obj_array_tail(struct workspace *wk, obj arr, obj *res)
{
	const struct obj_array *a = get_obj_array(wk, arr);

	*res = make_obj(wk, obj_array);

	if (a->len > 1) {
		struct obj_array *n = get_obj_array(wk, *res);
		struct obj_array_elem *head = bucket_arr_get(&wk->vm.objects.array_elems, a->head);

		n->head = head->next;
		n->tail = a->tail;
		n->len = a->len;
	}
}

void
obj_array_set(struct workspace *wk, obj arr, int64_t i, obj v)
{
	obj *p = obj_array_index_pointer(wk, arr, i);
	assert(p);
	*p = v;
}

void
obj_array_del(struct workspace *wk, obj arr, int64_t i)
{
	struct obj_array *a = get_obj_array(wk, arr);
	obj_array_copy_on_write(wk, a, arr);

	struct obj_array_elem *head = bucket_arr_get(&wk->vm.objects.array_elems, a->head), *prev = 0;
	uint32_t head_idx = a->head, prev_idx = 0;

	assert(i >= 0 && i < a->len);

#if 0
	if (i == 0) {
		if (a->len >= 1) {
			head = bucket_arr_get(&wk->vm.objects.array_elems, a->head);
			a->head = head->next;
			--a->len;
		} else {
			*a = (struct obj_array){ 0 };
		}

		return;
	}
#endif

	int64_t j = 0;
	while (true) {
		if (j == i) {
			break;
		}

		prev_idx = head_idx;
		prev = head;
		head_idx = head->next;
		head = bucket_arr_get(&wk->vm.objects.array_elems, head_idx);
		++j;
	}

	if (i == 0) {
		a->head = head->next;
	} else if (i == a->len - 1) {
		a->tail = prev_idx;
		prev->next = 0;
	} else {
		prev->next = head->next;
	}

	--a->len;
}

obj
obj_array_pop(struct workspace *wk, obj arr)
{
	obj t = obj_array_get_tail(wk, arr);
	obj_array_del(wk, arr, get_obj_array(wk, arr)->len - 1);
	return t;
}

void
obj_array_clear(struct workspace *wk, obj arr)
{
	struct obj_array *a = get_obj_array(wk, arr);
	*a = (struct obj_array){ 0 };
}

void
obj_array_dedup(struct workspace *wk, obj arr, obj *res)
{
	hash_clear(&wk->vm.objects.obj_hash);
	hash_clear(&wk->vm.objects.dedup_str_hash);

	*res = make_obj(wk, obj_array);

	obj val, oval;
	obj_array_for(wk, arr, val) {
		oval = val;
		switch (get_obj_type(wk, val)) {
		case obj_file:
			val = *get_obj_file(wk, val);
			/* fallthrough */
		case obj_string: {
			const struct str *s = get_str(wk, val);
			if (hash_get_strn(&wk->vm.objects.dedup_str_hash, s->s, s->len)) {
				continue;
			}
			hash_set_strn(&wk->vm.objects.dedup_str_hash, s->s, s->len, true);

			obj_array_push(wk, *res, oval);
			break;
		}
		default: {
			if (hash_get(&wk->vm.objects.obj_hash, &val)) {
				continue;
			}
			hash_set(&wk->vm.objects.obj_hash, &val, true);

			if (!obj_array_in(wk, *res, val)) {
				obj_array_push(wk, *res, val);
			}
			break;
		}
		}
	}
}

void
obj_array_dedup_in_place(struct workspace *wk, obj *arr)
{
	if (!*arr) {
		return;
	}

	obj dedupd;
	obj_array_dedup(wk, *arr, &dedupd);
	*arr = dedupd;
}

bool
obj_array_flatten_one(struct workspace *wk, obj val, obj *res)
{
	enum obj_type t = get_obj_type(wk, val);

	if (t == obj_array) {
		struct obj_array *v = get_obj_array(wk, val);

		if (v->len == 1) {
			*res = obj_array_index(wk, val, 0);
		} else {
			return false;
		}
	} else {
		*res = val;
	}

	return true;
}

int32_t
obj_array_sort_by_str(struct workspace *wk, void *_ctx, obj a, obj b)
{
	const struct str *sa = get_str(wk, a), *sb = get_str(wk, b);

	uint32_t min = sa->len > sb->len ? sb->len : sa->len;

	return memcmp(sa->s, sb->s, min);
}

static enum iteration_result
obj_array_sort_push_to_da_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct arr *da = _ctx;
	arr_push(da, &v);
	return ir_cont;
}

struct obj_array_sort_ctx {
	struct workspace *wk;
	void *usr_ctx;
	obj_array_sort_func func;
};

static int32_t
obj_array_sort_wrapper(const void *a, const void *b, void *_ctx)
{
	struct obj_array_sort_ctx *ctx = _ctx;

	return ctx->func(ctx->wk, ctx->usr_ctx, *(obj *)a, *(obj *)b);
}

void
obj_array_sort(struct workspace *wk, void *usr_ctx, obj arr, obj_array_sort_func func, obj *res)
{
	uint32_t len = get_obj_array(wk, arr)->len;

	if (!len) {
		*res = arr;
		return;
	}

	struct arr da;
	arr_init(&da, len, sizeof(obj));
	obj_array_foreach(wk, arr, &da, obj_array_sort_push_to_da_iter);

	struct obj_array_sort_ctx ctx = {
		.wk = wk,
		.usr_ctx = usr_ctx,
		.func = func,
	};

	arr_sort(&da, &ctx, obj_array_sort_wrapper);

	*res = make_obj(wk, obj_array);

	uint32_t i;
	for (i = 0; i < da.len; ++i) {
		obj_array_push(wk, *res, *(obj *)arr_get(&da, i));
	}

	arr_destroy(&da);
}

obj
obj_array_slice(struct workspace *wk, obj a, int64_t start, int64_t end)
{
	struct obj_array *src = get_obj_array(wk, a);

	obj res;
	res = make_obj(wk, obj_array);
	struct obj_array *dst = get_obj_array(wk, res);

	if (start == end) {
		// empty slice
		return res;
	}

	const bool tail_slice = end == src->len;
	uint32_t prev_elem = src->head;

	obj v;
	obj_array_for_array_(wk, src, v, iter) {
		if (iter.i >= end) {
			break;
		}

		if (iter.i >= start) {
			if (tail_slice) {
				src->flags |= obj_array_flag_cow;

				dst->len = src->len - start;
				dst->head = prev_elem;
				dst->tail = src->tail;
				dst->flags |= obj_array_flag_cow;
				return res;
			}

			obj_array_push(wk, res, v);
		}

		prev_elem = iter.e->next;
	}

	return res;
}

/*******************************************************************************
 * dictionaries
 ******************************************************************************/

bool
obj_dict_foreach(struct workspace *wk, obj dict, void *ctx, obj_dict_iterator cb)
{
	obj k, v;
	obj_dict_for(wk, dict, k, v) {
		switch (cb(wk, ctx, k, v)) {
		case ir_cont: break;
		case ir_done: return true;
		case ir_err: return false;
		}
	}

	return true;
}

void
obj_dict_dup(struct workspace *wk, obj dict, obj *res)
{
	*res = make_obj(wk, obj_dict);

	obj k, v;
	obj_dict_for(wk, dict, k, v) {
		obj_dict_set(wk, *res, k, v);
	}
}

void
obj_dict_dup_light(struct workspace *wk, obj dict, obj *res)
{
	*res = make_obj(wk, obj_dict);
	struct obj_dict *new = get_obj_dict(wk, *res), *cur = get_obj_dict(wk, dict);
	*new = *cur;
	// TODO: We have to set cow on both dicts because we don't know which one
	// will be modified.
	// Ideally, we would unset the one dict's cow flag when the other one gets
	// written to, but that would require refcounting.
	//
	// a = {}
	// b = a
	// c = a
	//
	// Now a, b, and c all point at the same underlying dict.  We'd need to
	// track the references and only remove cow when there is a single reference.
	cur->flags |= obj_dict_flag_cow;
	new->flags |= obj_dict_flag_cow;
}

void
obj_dict_merge_nodup(struct workspace *wk, obj dict, obj dict2)
{
	obj k, v;
	obj_dict_for(wk, dict2, k, v) {
		obj_dict_set(wk, dict, k, v);
	}
}

void
obj_dict_merge(struct workspace *wk, obj dict, obj dict2, obj *res)
{
	obj_dict_dup(wk, dict, res);
	obj_dict_merge_nodup(wk, *res, dict2);
}

union obj_dict_key_comparison_key {
	struct str string;
	uint32_t num;
};

/* other is marked uint32_t since it can be used to represent an obj or a number
 */
typedef bool(
	(*obj_dict_key_comparison_func)(struct workspace *wk, union obj_dict_key_comparison_key *key, uint32_t other));

static bool
obj_dict_key_comparison_func_string(struct workspace *wk, union obj_dict_key_comparison_key *key, obj other)
{
	const struct str *ss_a = get_str(wk, other);
	return str_eql(ss_a, &key->string);
}

static bool
obj_dict_key_comparison_func_int(struct workspace *wk, union obj_dict_key_comparison_key *key, uint32_t other)
{
	return key->num == other;
}

static bool
_obj_dict_index(struct workspace *wk,
	obj dict,
	union obj_dict_key_comparison_key *key,
	obj_dict_key_comparison_func comp,
	obj **res)
{
	struct obj_dict *d = get_obj_dict(wk, dict);
	if (!d->len) {
		return false;
	}

	if (d->flags & obj_dict_flag_big) {
		struct hash *h = bucket_arr_get(&wk->vm.objects.dict_hashes, d->data);
		uint64_t *uv;

		if (d->flags & obj_dict_flag_int_key) {
			uv = hash_get(h, &key->num);
		} else {
			uv = hash_get_strn(h, key->string.s, key->string.len);
		}

		if (uv) {
			union obj_dict_big_dict_value *val = (union obj_dict_big_dict_value *)uv;
			*res = &val->val.val;
			return true;
		}
	} else {
		struct obj_dict_elem *e = bucket_arr_get(&wk->vm.objects.dict_elems, d->data);

		while (true) {
			if (comp(wk, key, e->key)) {
				*res = &e->val;
				return true;
			}

			if (!e->next) {
				break;
			}
			e = bucket_arr_get(&wk->vm.objects.dict_elems, e->next);
		}
	}

	return false;
}

static obj *
obj_dict_index_strn_pointer_raw(struct workspace *wk, obj dict, const char *str, uint32_t len)
{
	obj *r = 0;
	union obj_dict_key_comparison_key key = { .string = { .s = str, .len = len } };

	if (!_obj_dict_index(wk, dict, &key, obj_dict_key_comparison_func_string, &r)) {
		return 0;
	}

	return r;
}

static void
obj_dict_copy_on_write(struct workspace *wk, obj dict)
{
	struct obj_dict *d = get_obj_dict(wk, dict), cur;

	if (!(d->flags & obj_dict_flag_cow)) {
		return;
	}

	cur = *d;
	*d = (struct obj_dict){ 0 };

	obj k, v;
	obj_dict_for_dict(wk, &cur, k, v)
	{
		obj_dict_set(wk, dict, k, v);
	}
}

obj *
obj_dict_index_strn_pointer(struct workspace *wk, obj dict, const char *str, uint32_t len)
{
	obj_dict_copy_on_write(wk, dict);
	return obj_dict_index_strn_pointer_raw(wk, dict, str, len);
}

bool
obj_dict_index_strn(struct workspace *wk, obj dict, const char *str, uint32_t len, obj *res)
{
	obj *r = obj_dict_index_strn_pointer_raw(wk, dict, str, len);
	if (r) {
		*res = *r;
		return true;
	}
	return false;
}

bool
obj_dict_index_str(struct workspace *wk, obj dict, const char *str, obj *res)
{
	return obj_dict_index_strn(wk, dict, str, strlen(str), res);
}

bool
obj_dict_index(struct workspace *wk, obj dict, obj key, obj *res)
{
	const struct str *k = get_str(wk, key);
	return obj_dict_index_strn(wk, dict, k->s, k->len, res);
}

bool
obj_dict_in(struct workspace *wk, obj dict, obj key)
{
	obj res;
	return obj_dict_index(wk, dict, key, &res);
}

static void
obj_dict_set_impl(struct workspace *wk,
	obj dict,
	union obj_dict_key_comparison_key *k,
	obj_dict_key_comparison_func comp,
	obj key,
	obj val)
{
	obj_dict_copy_on_write(wk, dict);

	struct obj_dict *d = get_obj_dict(wk, dict);

	/* empty dict */
	if (!d->len) {
		uint32_t e_idx = wk->vm.objects.dict_elems.len;
		bucket_arr_push(&wk->vm.objects.dict_elems, &(struct obj_dict_elem){ .key = key, .val = val });
		d->data = e_idx;
		d->tail = e_idx;
		++d->len;
		return;
	}

	if (!(d->flags & obj_dict_flag_dont_expand) && !(d->flags & obj_dict_flag_big) && d->len >= 15) {
		struct obj_dict_elem *e = bucket_arr_get(&wk->vm.objects.dict_elems, d->data);
		uint32_t h_idx = wk->vm.objects.dict_hashes.len;
		struct hash *h = bucket_arr_push(&wk->vm.objects.dict_hashes, &(struct hash){ 0 });
		if (d->flags & obj_dict_flag_int_key) {
			hash_init(h, 16, sizeof(obj));
		} else {
			hash_init_str(h, 16);
		}
		d->data = h_idx;
		d->tail = 0; // unnecessary but nice

		while (true) {
			union obj_dict_big_dict_value val = { .val = { .key = e->key, .val = e->val } };

			if (d->flags & obj_dict_flag_int_key) {
				hash_set(h, &e->key, val.u64);
			} else {
				const struct str *ss = get_str(wk, e->key);
				hash_set_strn(h, ss->s, ss->len, val.u64);
			}

			if (!e->next) {
				break;
			}
			e = bucket_arr_get(&wk->vm.objects.dict_elems, e->next);
		}
		d->flags |= obj_dict_flag_big;
	}

	obj *r = 0;
	if (_obj_dict_index(wk, dict, k, comp, &r)) {
		*r = val;
		return;
	}

	/* set new value */
	if ((d->flags & obj_dict_flag_big)) {
		struct hash *h = bucket_arr_get(&wk->vm.objects.dict_hashes, d->data);
		union obj_dict_big_dict_value big_val = { .val = { .key = key, .val = val } };

		if (d->flags & obj_dict_flag_int_key) {
			hash_set(h, &key, big_val.u64);
		} else {
			const struct str *ss = get_str(wk, key);
			hash_set_strn(h, ss->s, ss->len, big_val.u64);
		}
		d->len = h->len;
	} else {
		uint32_t e_idx = wk->vm.objects.dict_elems.len;
		bucket_arr_push(&wk->vm.objects.dict_elems,
			&(struct obj_dict_elem){
				.key = key,
				.val = val,
			});

		struct obj_dict_elem *tail = bucket_arr_get(&wk->vm.objects.dict_elems, d->tail);
		tail->next = e_idx;

		d->tail = e_idx;
		++d->len;
	}
}

void
obj_dict_set(struct workspace *wk, obj dict, obj key, obj val)
{
	union obj_dict_key_comparison_key k = {
		.string = *get_str(wk, key),
	};
	obj_dict_set_impl(wk, dict, &k, obj_dict_key_comparison_func_string, key, val);
}

static void
_obj_dict_del(struct workspace *wk, obj dict, union obj_dict_key_comparison_key *key, obj_dict_key_comparison_func comp)
{
	obj_dict_copy_on_write(wk, dict);

	struct obj_dict *d = get_obj_dict(wk, dict);
	if (!d->len) {
		return;
	}

	if (d->flags & obj_dict_flag_big) {
		struct hash *h = bucket_arr_get(&wk->vm.objects.dict_hashes, d->data);

		if (d->flags & obj_dict_flag_int_key) {
			hash_unset(h, &key->num);
		} else {
			hash_unset_strn(h, key->string.s, key->string.len);
		}

		return;
	}

	uint32_t cur_id = d->data, prev_id = 0;
	bool found = false;
	struct obj_dict_elem *prev, *e = bucket_arr_get(&wk->vm.objects.dict_elems, d->data);

	while (true) {
		if (comp(wk, key, e->key)) {
			found = true;
			break;
		}

		prev_id = cur_id;
		if (!e->next) {
			break;
		}
		cur_id = e->next;
		e = bucket_arr_get(&wk->vm.objects.dict_elems, e->next);
	}

	if (!found) {
		return;
	}

	--d->len;
	if (cur_id == d->data) {
		d->data = e->next;
	} else {
		prev = bucket_arr_get(&wk->vm.objects.dict_elems, prev_id);
		if (e->next) {
			prev->next = e->next;
		} else {
			d->tail = prev_id;
			prev->next = 0;
		}
	}
}

void
obj_dict_del_strn(struct workspace *wk, obj dict, const char *str, uint32_t len)
{
	union obj_dict_key_comparison_key key = { .string = {
							  .s = str,
							  .len = len,
						  } };
	_obj_dict_del(wk, dict, &key, obj_dict_key_comparison_func_string);
}

void
obj_dict_del_str(struct workspace *wk, obj dict, const char *str)
{
	obj_dict_del_strn(wk, dict, str, strlen(str));
}

void
obj_dict_del(struct workspace *wk, obj dict, obj key)
{
	const struct str *k = get_str(wk, key);
	obj_dict_del_strn(wk, dict, k->s, k->len);
}

/* dict convienence functions */

void
obj_dict_seti(struct workspace *wk, obj dict, uint32_t key, obj val)
{
	union obj_dict_key_comparison_key k = { .num = key };
	struct obj_dict *d = get_obj_dict(wk, dict);
	d->flags |= obj_dict_flag_int_key;
	obj_dict_set_impl(wk, dict, &k, obj_dict_key_comparison_func_int, key, val);
}

bool
obj_dict_geti(struct workspace *wk, obj dict, uint32_t key, obj *val)
{
	obj *r = 0;
	if (_obj_dict_index(wk,
		    dict,
		    &(union obj_dict_key_comparison_key){ .num = key },
		    obj_dict_key_comparison_func_int,
		    &r)) {
		*val = *r;
		return true;
	}

	return false;
}

const struct str *
obj_dict_index_as_str(struct workspace *wk, obj dict, const char *s)
{
	obj r;
	if (!obj_dict_index_str(wk, dict, s, &r)) {
		return 0;
	}

	return get_str(wk, r);
}

bool
obj_dict_index_as_bool(struct workspace *wk, obj dict, const char *s)
{
	obj r;
	if (!obj_dict_index_str(wk, dict, s, &r)) {
		return 0;
	}

	return get_obj_bool(wk, r);
}

int64_t
obj_dict_index_as_number(struct workspace *wk, obj dict, const char *s)
{
	obj r;
	if (!obj_dict_index_str(wk, dict, s, &r)) {
		UNREACHABLE;
	}

	return get_obj_number(wk, r);
}

obj
obj_dict_index_as_obj(struct workspace *wk, obj dict, const char *s)
{
	obj r;
	if (!obj_dict_index_str(wk, dict, s, &r)) {
		return 0;
	}

	return r;
}

/*******************************************************************************
 * obj_iterable_foreach
 ******************************************************************************/

struct obj_iterable_foreach_ctx {
	void *ctx;
	obj_dict_iterator cb;
};

static enum iteration_result
obj_iterable_foreach_array_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_iterable_foreach_ctx *ctx = _ctx;
	return ctx->cb(wk, ctx->ctx, val, 0);
}

bool
obj_iterable_foreach(struct workspace *wk, obj dict_or_array, void *ctx, obj_dict_iterator cb)
{
	switch (get_obj_type(wk, dict_or_array)) {
	case obj_dict: {
		return obj_dict_foreach(wk, dict_or_array, ctx, cb);
	}
	case obj_array: {
		return obj_array_foreach(wk,
			dict_or_array,
			&(struct obj_iterable_foreach_ctx){
				.ctx = ctx,
				.cb = cb,
			},
			obj_iterable_foreach_array_iter);
	}
	default: UNREACHABLE_RETURN;
	}
}

/* */

struct obj_clone_ctx {
	struct workspace *wk_dest;
	obj container;
};

static enum iteration_result
obj_clone_array_iter(struct workspace *wk_src, void *_ctx, obj val)
{
	struct obj_clone_ctx *ctx = _ctx;

	obj dest_val;

	if (!obj_clone(wk_src, ctx->wk_dest, val, &dest_val)) {
		return ir_err;
	}

	obj_array_push(ctx->wk_dest, ctx->container, dest_val);
	return ir_cont;
}

static enum iteration_result
obj_clone_dict_iter(struct workspace *wk_src, void *_ctx, obj key, obj val)
{
	struct obj_clone_ctx *ctx = _ctx;

	obj dest_key, dest_val;

	if (!obj_clone(wk_src, ctx->wk_dest, key, &dest_key)) {
		return ir_err;
	} else if (!obj_clone(wk_src, ctx->wk_dest, val, &dest_val)) {
		return ir_err;
	}

	obj_dict_set(ctx->wk_dest, ctx->container, dest_key, dest_val);
	return ir_cont;
}

bool
obj_clone(struct workspace *wk_src, struct workspace *wk_dest, obj val, obj *ret)
{
	if (val >= wk_src->vm.objects.objs.len) {
		LOG_E("invalid object");
		return false;
	}

	enum obj_type t = get_obj_type(wk_src, val);
	/* L("cloning %s", obj_type_to_s(t)); */

	switch (t) {
	case obj_null: *ret = 0; return true;
	case obj_number:
		*ret = make_obj(wk_dest, t);
		set_obj_number(wk_dest, *ret, get_obj_number(wk_src, val));
		return true;
	case obj_disabler: *ret = val; return true;
	case obj_bool: *ret = val; return true;
	case obj_string: {
		*ret = str_clone(wk_src, wk_dest, val);
		return true;
	}
	case obj_file:
		*ret = make_obj(wk_dest, t);
		*get_obj_file(wk_dest, *ret) = str_clone(wk_src, wk_dest, *get_obj_file(wk_src, val));
		return true;
	case obj_array:
		*ret = make_obj(wk_dest, t);
		return obj_array_foreach(wk_src,
			val,
			&(struct obj_clone_ctx){ .container = *ret, .wk_dest = wk_dest },
			obj_clone_array_iter);
	case obj_dict:
		*ret = make_obj(wk_dest, t);
		struct obj_dict *d = get_obj_dict(wk_dest, *ret);
		d->flags |= obj_dict_flag_dont_expand;
		bool status = obj_dict_foreach(wk_src,
			val,
			&(struct obj_clone_ctx){ .container = *ret, .wk_dest = wk_dest },
			obj_clone_dict_iter);
		d->flags &= ~obj_dict_flag_dont_expand;
		return status;
	case obj_test: {
		*ret = make_obj(wk_dest, t);
		struct obj_test *test = get_obj_test(wk_src, val), *o = get_obj_test(wk_dest, *ret);
		*o = *test;

		o->name = str_clone(wk_src, wk_dest, test->name);
		o->exe = str_clone(wk_src, wk_dest, test->exe);
		if (test->workdir) {
			o->workdir = str_clone(wk_src, wk_dest, test->workdir);
		}

		if (!obj_clone(wk_src, wk_dest, test->args, &o->args)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, test->env, &o->env)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, test->suites, &o->suites)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, test->depends, &o->depends)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, test->timeout, &o->timeout)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, test->priority, &o->priority)) {
			return false;
		}
		return true;
	}
	case obj_install_target: {
		*ret = make_obj(wk_dest, t);
		struct obj_install_target *in = get_obj_install_target(wk_src, val),
					  *o = get_obj_install_target(wk_dest, *ret);

		o->src = str_clone(wk_src, wk_dest, in->src);
		o->dest = str_clone(wk_src, wk_dest, in->dest);
		o->build_target = in->build_target;
		o->type = in->type;

		o->has_perm = in->has_perm;
		o->perm = in->perm;

		if (!obj_clone(wk_src, wk_dest, in->exclude_directories, &o->exclude_directories)) {
			return false;
		}
		if (!obj_clone(wk_src, wk_dest, in->exclude_files, &o->exclude_files)) {
			return false;
		}
		return true;
	}
	case obj_environment: {
		*ret = make_obj(wk_dest, obj_environment);
		struct obj_environment *env = get_obj_environment(wk_src, val), *o = get_obj_environment(wk_dest, *ret);

		if (!obj_clone(wk_src, wk_dest, env->actions, &o->actions)) {
			return false;
		}
		return true;
	}
	case obj_option: {
		*ret = make_obj(wk_dest, t);
		struct obj_option *opt = get_obj_option(wk_src, val), *o = get_obj_option(wk_dest, *ret);

		o->source = opt->source;
		o->type = opt->type;
		o->builtin = opt->builtin;
		o->yield = opt->yield;

		if (!obj_clone(wk_src, wk_dest, opt->name, &o->name)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, opt->val, &o->val)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, opt->choices, &o->choices)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, opt->max, &o->max)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, opt->min, &o->min)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, opt->deprecated, &o->deprecated)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, opt->description, &o->description)) {
			return false;
		}
		return true;
	}
	case obj_feature_opt: {
		*ret = make_obj(wk_dest, t);

		set_obj_feature_opt(wk_dest, *ret, get_obj_feature_opt(wk_src, val));
		return true;
	}
	case obj_configuration_data: {
		*ret = make_obj(wk_dest, t);
		struct obj_configuration_data *conf = get_obj_configuration_data(wk_src, val),
					      *o = get_obj_configuration_data(wk_dest, *ret);

		if (!obj_clone(wk_src, wk_dest, conf->dict, &o->dict)) {
			return false;
		}
		return true;
	}
	case obj_run_result: {
		*ret = make_obj(wk_dest, t);
		struct obj_run_result *rr = get_obj_run_result(wk_src, val), *o = get_obj_run_result(wk_dest, *ret);

		*o = *rr;

		if (!obj_clone(wk_src, wk_dest, rr->out, &o->out)) {
			return false;
		} else if (!obj_clone(wk_src, wk_dest, rr->err, &o->err)) {
			return false;
		}
		return true;
	}
	default: LOG_E("unable to clone '%s'", obj_type_to_s(t)); return false;
	}
}

struct obj_to_s_opts {
	bool pretty;
	uint32_t indent;
};

struct obj_to_s_ctx {
	struct tstr *sb;
	struct obj_to_s_opts *opts;
	uint32_t cont_i, cont_len;
};

static void obj_to_s_opts(struct workspace *wk, obj o, struct tstr *sb, struct obj_to_s_opts *opts);

static void
obj_to_s_indent(struct workspace *wk, struct obj_to_s_ctx *ctx)
{
	if (!ctx->opts->pretty) {
		return;
	}

	uint32_t i;
	for (i = 0; i < ctx->opts->indent; ++i) {
		tstr_pushs(wk, ctx->sb, "  ");
	}
}

static void
obj_to_s_pretty_newline(struct workspace *wk, struct obj_to_s_ctx *ctx)
{
	if (!ctx->opts->pretty) {
		return;
	}

	tstr_push(wk, ctx->sb, '\n');
	obj_to_s_indent(wk, ctx);
}

static void
obj_to_s_pretty_newline_or_space(struct workspace *wk, struct obj_to_s_ctx *ctx)
{
	if (!ctx->opts->pretty) {
		tstr_push(wk, ctx->sb, ' ');
		return;
	}

	obj_to_s_pretty_newline(wk, ctx);
}

static enum iteration_result
obj_to_s_array_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_to_s_ctx *ctx = _ctx;

	obj_to_s_opts(wk, val, ctx->sb, ctx->opts);

	if (ctx->cont_i < ctx->cont_len - 1) {
		tstr_pushs(wk, ctx->sb, ",");
		obj_to_s_pretty_newline_or_space(wk, ctx);
	}

	++ctx->cont_i;
	return ir_cont;
}

static void
obj_to_s_str(struct workspace *wk, struct obj_to_s_ctx *ctx, obj s)
{
	tstr_push(wk, ctx->sb, '\'');
	str_escape(wk, ctx->sb, get_str(wk, s), true);
	tstr_push(wk, ctx->sb, '\'');
}

static void
obj_to_s_opts(struct workspace *wk, obj o, struct tstr *sb, struct obj_to_s_opts *opts)
{
	struct obj_to_s_ctx ctx = { .sb = sb, .opts = opts };
	enum obj_type t = get_obj_type(wk, o);

	switch (t) {
	case obj_include_directory: {
		struct obj_include_directory *inc = get_obj_include_directory(wk, o);
		tstr_pushs(wk, sb, "<include_directory ");
		obj_to_s_str(wk, &ctx, inc->path);
		tstr_pushs(wk, sb, ">");
		break;
	}
	case obj_dependency: {
		struct obj_dependency *dep = get_obj_dependency(wk, o);
		tstr_pushs(wk, sb, "<dep ");
		if (dep->name) {
			obj_to_s_str(wk, &ctx, dep->name);
		}

		const char *type = 0;
		switch (dep->type) {
		case dependency_type_declared: type = "declared"; break;
		case dependency_type_pkgconf: type = "pkgconf"; break;
		case dependency_type_threads: type = "threads"; break;
		case dependency_type_external_library: type = "external_library"; break;
		case dependency_type_system: type = "system"; break;
		case dependency_type_not_found: type = "not_found"; break;
		}

		const char *found = dep->flags & dep_flag_found ? " found" : "";

		tstr_pushf(wk, sb, " %s machine:%s%s>", type, machine_kind_to_s(dep->machine), found);
		break;
	}
	case obj_alias_target:
		tstr_pushs(wk, sb, "<alias_target ");
		obj_to_s_str(wk, &ctx, get_obj_alias_target(wk, o)->name);
		tstr_pushs(wk, sb, ">");
		break;
	case obj_build_target: {
		struct obj_build_target *tgt = get_obj_build_target(wk, o);
		const char *type = NULL;
		switch (tgt->type) {
		case tgt_executable: type = "executable"; break;
		case tgt_static_library: type = "static_library"; break;
		case tgt_dynamic_library: type = "shared_library"; break;
		case tgt_shared_module: type = "shared_module"; break;
		}

		tstr_pushf(wk, sb, "<%s ", type);
		obj_to_s_str(wk, &ctx, tgt->name);
		tstr_pushs(wk, sb, ">");

		break;
	}
	case obj_feature_opt:
		switch (get_obj_feature_opt(wk, o)) {
		case feature_opt_auto: tstr_pushs(wk, sb, "'auto'"); break;
		case feature_opt_enabled: tstr_pushs(wk, sb, "'enabled'"); break;
		case feature_opt_disabled: tstr_pushs(wk, sb, "'disabled'"); break;
		}

		break;
	case obj_test: {
		struct obj_test *test = get_obj_test(wk, o);
		tstr_pushs(wk, sb, "test(");
		obj_to_s_str(wk, &ctx, test->name);
		tstr_pushs(wk, sb, ", ");
		obj_to_s_str(wk, &ctx, test->exe);

		if (test->args) {
			tstr_pushs(wk, sb, ", args: ");
			obj_to_s_opts(wk, test->args, sb, opts);
		}

		if (test->should_fail) {
			tstr_pushs(wk, sb, ", should_fail: true");
		}

		tstr_pushs(wk, sb, ")");
		break;
	}
	case obj_file:
		tstr_pushs(wk, sb, "<file ");
		obj_to_s_str(wk, &ctx, *get_obj_file(wk, o));
		tstr_pushs(wk, sb, ">");
		break;
	case obj_string: {
		obj_to_s_str(wk, &ctx, o);
		break;
	}
	case obj_number: tstr_pushf(wk, sb, "%" PRId64, get_obj_number(wk, o)); break;
	case obj_bool: tstr_pushs(wk, sb, get_obj_bool(wk, o) ? "true" : "false"); break;
	case obj_array:
		ctx.cont_len = get_obj_array(wk, o)->len;

		tstr_pushs(wk, sb, "[");
		++opts->indent;
		obj_to_s_pretty_newline(wk, &ctx);

		obj_array_foreach(wk, o, &ctx, obj_to_s_array_iter);

		--opts->indent;
		obj_to_s_pretty_newline(wk, &ctx);
		tstr_pushs(wk, sb, "]");
		break;
	case obj_dict:
		ctx.cont_len = get_obj_dict(wk, o)->len;

		tstr_pushs(wk, sb, "{");
		++opts->indent;
		obj_to_s_pretty_newline(wk, &ctx);

		bool int_keys = get_obj_dict(wk, o)->flags & obj_dict_flag_int_key;

		obj key, val;
		obj_dict_for(wk, o, key, val)
		{
			if (int_keys) {
				tstr_pushf(wk, ctx.sb, "%d", key);
			} else {
				obj_to_s_opts(wk, key, ctx.sb, ctx.opts);
			}

			tstr_pushs(wk, ctx.sb, ": ");

			obj_to_s_opts(wk, val, ctx.sb, ctx.opts);

			if (ctx.cont_i < ctx.cont_len - 1) {
				tstr_pushs(wk, ctx.sb, ",");
				obj_to_s_pretty_newline_or_space(wk, &ctx);
			}

			++ctx.cont_i;
		}

		--opts->indent;
		obj_to_s_pretty_newline(wk, &ctx);
		tstr_pushs(wk, sb, "}");
		break;
	case obj_python_installation: {
		struct obj_python_installation *py = get_obj_python_installation(wk, o);
		tstr_pushf(wk, sb, "<%s prog: ", obj_type_to_s(t));
		obj_to_s_opts(wk, py->prog, sb, opts);

		if (get_obj_external_program(wk, py->prog)->found) {
			tstr_pushf(wk, sb, ", pure: %s", py->pure ? "true" : "false");
			tstr_pushf(wk, sb, ", language_version: %s", get_cstr(wk, py->language_version));
			tstr_pushs(wk, sb, ", sysconfig_paths: ");
			obj_to_s_opts(wk, py->sysconfig_paths, sb, opts);
			tstr_pushs(wk, sb, ", sysconfig_vars: ");
			obj_to_s_opts(wk, py->sysconfig_vars, sb, opts);
			tstr_pushs(wk, sb, ", install_paths: ");
			obj_to_s_opts(wk, py->install_paths, sb, opts);
		}

		tstr_pushs(wk, sb, ">");
		break;
	}
	case obj_external_program: {
		struct obj_external_program *prog = get_obj_external_program(wk, o);
		tstr_pushf(wk, sb, "<%s found: %s", obj_type_to_s(t), prog->found ? "true" : "false");

		if (prog->found) {
			tstr_pushs(wk, sb, ", cmd_array: ");
			obj_to_s_opts(wk, prog->cmd_array, sb, opts);
		}

		tstr_pushs(wk, sb, ">");
		break;
	}
	case obj_option: {
		struct obj_option *opt = get_obj_option(wk, o);
		tstr_pushs(wk, sb, "<option ");

		obj_to_s_opts(wk, opt->val, sb, opts);

		tstr_pushs(wk, sb, ">");
		break;
	}
	case obj_generated_list: {
		struct obj_generated_list *gl = get_obj_generated_list(wk, o);
		tstr_pushs(wk, sb, "<generated_list input: ");

		obj_to_s_opts(wk, gl->input, sb, opts);

		/* tstr_pushs(wk, sb, ", extra_args: "); */
		/* obj_to_s_opts(wk, gl->extra_arguments, sb, opts); */

		/* tstr_pushs(wk, sb, ", preserve_path_from: "); */
		/* obj_to_s_opts(wk, gl->preserve_path_from, sb, opts); */

		tstr_pushs(wk, sb, ">");

		break;
	}
	case obj_typeinfo: {
		struct obj_typeinfo *ti = get_obj_typeinfo(wk, o);
		tstr_pushf(wk, sb, "<typeinfo 0x4%x: ", o);
		tstr_pushs(wk, sb, typechecking_type_to_s(wk, ti->type));
		tstr_pushs(wk, sb, ">");
		break;
	}
	default: tstr_pushf(wk, sb, "<obj %s>", obj_type_to_s(t));
	}
}

void
obj_to_s(struct workspace *wk, obj o, struct tstr *sb)
{
	struct obj_to_s_opts opts = {
		.pretty = false,
	};

	obj_to_s_opts(wk, o, sb, &opts);
}

#define FMT_PARTIAL(arg)                                                       \
	if (arg_width.have && arg_prec.have) {                                 \
		tstr_pushf(wk, sb, fmt_buf, arg_width.val, arg_prec.val, arg); \
	} else if (arg_width.have) {                                           \
		tstr_pushf(wk, sb, fmt_buf, arg_width.val, arg);               \
	} else if (arg_prec.have) {                                            \
		tstr_pushf(wk, sb, fmt_buf, arg_prec.val, arg);                \
	} else {                                                               \
		tstr_pushf(wk, sb, fmt_buf, arg);                              \
	}

bool
obj_vasprintf(struct workspace *wk, struct tstr *sb, const char *fmt, va_list ap)
{
	const char *fmt_start;
	bool got_hash;

	struct {
		int val;
		bool have;
	} arg_width = { 0 }, arg_prec = { 0 };

	for (; *fmt; ++fmt) {
		if (*fmt == '%') {
			arg_width.have = false;
			arg_prec.have = false;

			got_hash = false;
			fmt_start = fmt;
			++fmt;

			// skip flags
			while (strchr("#0- +", *fmt)) {
				if (*fmt == '#') {
					got_hash = true;
				}
				++fmt;
			}

			// skip field width
			if (*fmt == '*') {
				arg_width.val = va_arg(ap, int);
				arg_width.have = true;
				++fmt;
			} else {
				while (strchr("1234567890", *fmt)) {
					++fmt;
				}
			}

			// skip precision
			if (*fmt == '.') {
				++fmt;

				if (*fmt == '*') {
					arg_prec.val = va_arg(ap, int);
					arg_prec.have = true;
					++fmt;
				} else {
					while (strchr("1234567890", *fmt)) {
						++fmt;
					}
				}
			}

			enum {
				il_norm,
				il_long,
				il_long_long,
			} int_len
				= il_norm;

			switch (*fmt) {
			case 'l':
				int_len = il_long;
				++fmt;
				break;
			case 'h':
			case 'L':
			case 'j':
			case 'z':
			case 't': assert(false && "unimplemented length modifier"); break;
			}

			if (int_len == il_long && *fmt == 'l') {
				int_len = il_long_long;
				++fmt;
			}

			if (*fmt == 'o') {
				obj o = va_arg(ap, unsigned int);
				if (get_obj_type(wk, o) == obj_string && got_hash) {
					str_escape(wk, sb, get_str(wk, o), false);
				} else {
					struct obj_to_s_opts opts = { 0 };

					if (got_hash) {
						opts.pretty = true;
					}

					obj_to_s_opts(wk, o, sb, &opts);
				}

				continue;
			}

			char fmt_buf[BUF_SIZE_1k + 1] = { 0 };
			uint32_t len = fmt - fmt_start + 1;
			assert(len < BUF_SIZE_1k && "format specifier too long");
			memcpy(fmt_buf, fmt_start, len);

			switch (*fmt) {
			case 'c':
			case 'd':
			case 'i':
				switch (int_len) {
				case il_norm: {
					FMT_PARTIAL(va_arg(ap, int));
					break;
				}
				case il_long: {
					FMT_PARTIAL(va_arg(ap, long));
					break;
				}
				case il_long_long: {
					FMT_PARTIAL(va_arg(ap, long long));
					break;
				}
				}
				break;
			case 'u':
			case 'x':
			case 'X':
				switch (int_len) {
				case il_norm: {
					FMT_PARTIAL(va_arg(ap, unsigned int));

					break;
				}
				case il_long: {
					FMT_PARTIAL(va_arg(ap, unsigned long));
					break;
				}
				case il_long_long: {
					FMT_PARTIAL(va_arg(ap, unsigned long long));
					break;
				}
				}
				break;
			case 'e':
			case 'E':
			case 'f':
			case 'F':
			case 'g':
			case 'G':
			case 'a':
			case 'A': {
				FMT_PARTIAL(va_arg(ap, double));
				break;
			}
			case 's': {
				FMT_PARTIAL(va_arg(ap, char *));
				break;
			}
			case 'p': {
				FMT_PARTIAL(va_arg(ap, void *));
				break;
			}
			case 'n':
			case '%': break;
			default: assert(false && "unrecognized format"); break;
			}
		} else {
			tstr_push(wk, sb, *fmt);
		}
	}

	va_end(ap);
	return true;
}

uint32_t
obj_vsnprintf(struct workspace *wk, char *buf, uint32_t len, const char *fmt, va_list ap)
{
	TSTR(tstr);

	if (!obj_vasprintf(wk, &tstr, fmt, ap)) {
		return 0;
	}
	uint32_t copy = tstr.len > len - 1 ? len - 1 : tstr.len;

	strncpy(buf, tstr.buf, len - 1);
	return copy;
}

uint32_t
obj_asprintf(struct workspace *wk, struct tstr *buf, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	uint32_t ret = obj_vasprintf(wk, buf, fmt, ap);
	va_end(ap);
	return ret;
}

uint32_t
obj_snprintf(struct workspace *wk, char *buf, uint32_t len, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	uint32_t ret = obj_vsnprintf(wk, buf, len, fmt, ap);
	va_end(ap);
	return ret;
}

static bool
obj_vfprintf(struct workspace *wk, FILE *f, const char *fmt, va_list ap)
{
	TSTR_FILE(sb, f);

	return obj_vasprintf(wk, &sb, fmt, ap);
}

bool
obj_fprintf(struct workspace *wk, FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	bool ret = obj_vfprintf(wk, f, fmt, ap);
	va_end(ap);
	return ret;
}

bool
obj_printf(struct workspace *wk, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	bool ret = obj_vfprintf(wk, stdout, fmt, ap);
	va_end(ap);
	return ret;
}

bool
obj_lprintf(struct workspace *wk, enum log_level lvl, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	TSTR(sb);
	bool ret = obj_vasprintf(wk, &sb, fmt, ap);
	va_end(ap);

	log_printn(lvl, sb.buf, sb.len);

	return ret;
}

/*
 * inspect - obj_to_s + more detail for some objects
 */

static void
obj_inspect_dep(struct workspace *wk, const char *pre, struct build_dep *dep)
{
	obj_lprintf(wk, log_info, "%slink_language: %s\n", pre, compiler_language_to_s(dep->link_language));
	obj_lprintf(wk, log_info, "%slink_whole: %o\n", pre, dep->link_whole);
	obj_lprintf(wk, log_info, "%slink_with: %o\n", pre, dep->link_with);
	obj_lprintf(wk, log_info, "%slink_with_not_found: %o\n", pre, dep->link_with_not_found);
	obj_lprintf(wk, log_info, "%slink_args: %o\n", pre, dep->link_args);
	obj_lprintf(wk, log_info, "%scompile_args: %o\n", pre, dep->compile_args);
	obj_lprintf(wk, log_info, "%sinclude_directories: %o\n", pre, dep->include_directories);
	obj_lprintf(wk, log_info, "%ssources: %o\n", pre, dep->sources);
	obj_lprintf(wk, log_info, "%sobjects: %o\n", pre, dep->objects);
	obj_lprintf(wk, log_info, "%sorder_deps: %o\n", pre, dep->order_deps);
	obj_lprintf(wk, log_info, "%srpath: %o\n", pre, dep->rpath);
	obj_lprintf(wk, log_info, "%sframeworks: %o\n", pre, dep->frameworks);
}

void
obj_inspect(struct workspace *wk, obj val)
{
	enum log_level lvl = log_info;

	switch (get_obj_type(wk, val)) {
	case obj_build_target: {
		struct obj_build_target *tgt = get_obj_build_target(wk, val);

		log_plain(lvl, "build_target:\n");
		if (tgt->name) {
			obj_lprintf(wk, lvl, "    name: %o,\n", tgt->name);
		}
		obj_lprintf(wk, lvl, "    dep:\n");
		obj_inspect_dep(wk, "        ", &tgt->dep);
		obj_lprintf(wk, lvl, "    dep_internal:\n");
		obj_inspect_dep(wk, "        ", &tgt->dep_internal);
		break;
	}
	case obj_dependency: {
		struct obj_dependency *dep = get_obj_dependency(wk, val);

		log_plain(lvl, "dependency:\n");

		obj_lprintf(wk, lvl, "    found: %s\n", (dep->flags & dep_flag_found) ? "yes" : "no");
		obj_lprintf(wk, lvl, "    machine: %s\n", machine_kind_to_s(dep->machine));

		if (dep->name) {
			obj_lprintf(wk, lvl, "    name: %o\n", dep->name);
		}
		if (dep->version) {
			obj_lprintf(wk, lvl, "    version: %o\n", dep->version);
		}
		if (dep->variables) {
			obj_lprintf(wk, lvl, "    variables: '%o'\n", dep->variables);
		}

		obj_lprintf(wk, lvl, "    type: %d\n", dep->type);
		obj_lprintf(wk, lvl, "    dep:\n");

		obj_inspect_dep(wk, "        ", &dep->dep);
		break;
	}
	case obj_compiler: {
		struct obj_compiler *compiler = get_obj_compiler(wk, val);
		log_plain(lvl, "toolchain:\n");
		obj_lprintf(wk, lvl, "  ver: %o\n", compiler->ver);
		obj_lprintf(wk, lvl, "  libdirs: %o\n", compiler->libdirs);
		obj_lprintf(wk, lvl, "  lang: %s\n", compiler_language_to_s(compiler->lang));
		obj_lprintf(wk, lvl, "  machine: %s\n", machine_kind_to_s(compiler->machine));
		for (uint32_t i = 0; i < toolchain_component_count; ++i) {
			log_plain(lvl, "  %s:\n", toolchain_component_to_s(i));
			log_plain(lvl, "    type: %s\n", toolchain_component_type_to_s(i, compiler->type[i])->id);
			obj_lprintf(wk, lvl, "    cmd_arr: %o\n", compiler->cmd_arr[i]);
			obj_lprintf(wk, lvl, "    overrides: %o\n", compiler->overrides[i]);
		}
		break;
	}
	default: obj_lprintf(wk, lvl, "%o\n", val);
	}
}

/*
 * object to json
 */

bool
obj_to_json(struct workspace *wk, obj o, struct tstr *sb)
{
	switch (get_obj_type(wk, o)) {
	case obj_array: {
		tstr_push(wk, sb, '[');
		obj v;
		obj_array_for_(wk, o, v, iter)
		{
			if (!obj_to_json(wk, v, sb)) {
				return false;
			}

			if (iter.i < iter.len - 1) {
				tstr_pushs(wk, sb, ", ");
			}
		}
		tstr_push(wk, sb, ']');
		break;
	}
	case obj_dict: {
		tstr_push(wk, sb, '{');
		uint32_t i = 0, len = get_obj_dict(wk, o)->len;
		obj k, v;
		obj_dict_for(wk, o, k, v) {
			if (!obj_to_json(wk, k, sb)) {
				return false;
			}
			tstr_pushs(wk, sb, ": ");
			if (!obj_to_json(wk, v, sb)) {
				return false;
			}

			if (i < len - 1) {
				tstr_pushs(wk, sb, ", ");
			}
			++i;
		}
		tstr_push(wk, sb, '}');
		break;
	}
	case obj_number: tstr_pushf(wk, sb, "%" PRId64, get_obj_number(wk, o)); break;
	case obj_bool: tstr_pushs(wk, sb, get_obj_bool(wk, o) ? "true" : "false"); break;
	case obj_file:
		o = *get_obj_file(wk, o);
		/* fallthrough */
	case obj_string: {
		tstr_push(wk, sb, '\"');
		str_escape_json(wk, sb, get_str(wk, o));
		tstr_push(wk, sb, '\"');
		break;
	}
	case obj_feature_opt: {
		const char *s = (char *[]){
			[feature_opt_enabled] = "enabled",
			[feature_opt_disabled] = "disabled",
			[feature_opt_auto] = "auto",
		}[get_obj_feature_opt(wk, o)];
		tstr_pushf(wk, sb, "\"%s\"", s);
		break;
	}
	case obj_null: {
		tstr_pushs(wk, sb, "null");
		break;
	}
	default: vm_error(wk, "unable to convert %s to json", obj_type_to_s(get_obj_type(wk, o)));
			 return false;
	}

	return true;
}
