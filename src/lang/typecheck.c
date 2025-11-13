/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <inttypes.h>

#include "error.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"

type_tag
obj_type_to_tc_type(enum obj_type t)
{
	if (!t) {
		return obj_typechecking_type_tag;
	}

	assert(t - 1 < tc_type_count);
	return (((type_tag)1) << (t - 1)) | obj_typechecking_type_tag;
}

type_tag
make_complex_type(struct workspace *wk, enum complex_type t, type_tag type, type_tag subtype)
{
	struct bucket_arr *typeinfo_arr = &wk->vm.objects.obj_aos[obj_typeinfo - _obj_aos_start];
	uint32_t idx = typeinfo_arr->len;
	bucket_arr_push(wk->a, typeinfo_arr, &(struct obj_typeinfo){ .type = type, .subtype = subtype });
	return COMPLEX_TYPE(idx, t);
}

/*
 * ----------------------------------------------------------------------------
 *  type to typestr
 * ----------------------------------------------------------------------------
 */

static obj
simple_type_to_arr(struct workspace *wk, type_tag t)
{
	obj expected_types;
	expected_types = make_obj(wk, obj_array);

	if (!(t & obj_typechecking_type_tag)) {
		t = obj_type_to_tc_type(t);
	}

	if ((t & tc_any) == tc_any) {
		obj_array_push(wk, expected_types, make_str(wk, "any"));
		t &= ~tc_any;
	} else if ((t & tc_exe) == tc_exe) {
		obj_array_push(wk, expected_types, make_str(wk, "exe"));
		t &= ~tc_exe;
	}

	type_tag ot;
	for (ot = 1; ot <= tc_type_count; ++ot) {
		type_tag tc = obj_type_to_tc_type(ot);
		if ((t & tc) != tc) {
			continue;
		}

		obj_array_push(wk, expected_types, make_str(wk, obj_type_to_s(ot)));
	}

	if (!get_obj_array(wk, expected_types)->len) {
		obj_array_push(wk, expected_types, make_str(wk, "null"));
	}

	obj sorted;
	obj_array_sort(wk, NULL, expected_types, obj_array_sort_by_str, &sorted);

	return sorted;
}

obj
typechecking_type_to_arr(struct workspace *wk, type_tag t)
{
	if (!(t & TYPE_TAG_COMPLEX)) {
		return simple_type_to_arr(wk, t);
	}

	uint32_t idx = COMPLEX_TYPE_INDEX(t);
	enum complex_type ct = COMPLEX_TYPE_TYPE(t);

	if (ct == complex_type_preset) {
		return typechecking_type_to_arr(wk, complex_type_preset_get(wk, idx));
	} else if (ct == complex_type_enum) {
		obj sorted;
		obj_array_sort(wk, NULL, idx, obj_array_sort_by_str, &sorted);
		obj typestr;
		obj_array_join(wk, false, sorted, make_str(wk, "|"), &typestr);

		typestr = make_strf(wk, "%s[%s]", typechecking_type_to_s(wk, tc_string), get_str(wk, typestr)->s);

		obj res;
		res = make_obj(wk, obj_array);
		obj_array_push(wk, res, typestr);
		return res;
	}

	struct bucket_arr *typeinfo_arr = &wk->vm.objects.obj_aos[obj_typeinfo - _obj_aos_start];
	struct obj_typeinfo *ti = bucket_arr_get(typeinfo_arr, idx);

	obj typestr = typechecking_type_to_str(wk, ti->type);

	if (!ti->subtype) {
		obj res;
		res = make_obj(wk, obj_array);
		obj_array_push(wk, res, typestr);
		return res;
	}

	switch (ct) {
	case complex_type_or: {
		obj subtype_arr = typechecking_type_to_arr(wk, ti->subtype);
		obj_array_push(wk, subtype_arr, typestr);
		obj sorted;
		obj_array_sort(wk, NULL, subtype_arr, obj_array_sort_by_str, &sorted);
		return sorted;
	}
	case complex_type_nested: {
		str_appf(wk, &typestr, "[%s]", typechecking_type_to_s(wk, ti->subtype));

		obj res;
		res = make_obj(wk, obj_array);
		obj_array_push(wk, res, typestr);
		return res;
	}
	case complex_type_enum: break;
	case complex_type_preset: break;
	}

	UNREACHABLE_RETURN;
}

obj
typechecking_type_to_str(struct workspace *wk, type_tag t)
{
	obj typestr;

	t &= ~TYPE_TAG_ALLOW_NULL;

	const char *modifier = 0;
	if (t & TYPE_TAG_GLOB) {
		t &= ~TYPE_TAG_GLOB;
		modifier = "glob";
	} else if ((t & TYPE_TAG_LISTIFY)) {
		t &= ~TYPE_TAG_LISTIFY;
		modifier = "listify";
	}

	obj_array_join(wk, false, typechecking_type_to_arr(wk, t), make_str(wk, "|"), &typestr);
	if (modifier) {
		typestr = make_strf(wk, "%s[%s]", modifier, get_cstr(wk, typestr));
	}

	return typestr;
}

const char *
typechecking_type_to_s(struct workspace *wk, type_tag t)
{
	return get_cstr(wk, typechecking_type_to_str(wk, t));
}

/*
 * ----------------------------------------------------------------------------
 *  obj to typestr
 * ----------------------------------------------------------------------------
 */

struct obj_type_to_typestr_ctx {
	obj arr;
	uint32_t depth;
};

static obj _obj_type_to_typestr(struct workspace *wk, obj o, uint32_t depth);

static enum iteration_result
obj_type_to_typestr_arr_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct obj_type_to_typestr_ctx *ctx = _ctx;

	obj s = _obj_type_to_typestr(wk, v, ctx->depth);

	if (!obj_array_in(wk, ctx->arr, s)) {
		obj_array_push(wk, ctx->arr, s);
	}
	return ir_cont;
}

static enum iteration_result
obj_type_to_typestr_dict_iter(struct workspace *wk, void *_ctx, obj _k, obj v)
{
	return obj_type_to_typestr_arr_iter(wk, _ctx, v);
}

static obj
_obj_type_to_typestr(struct workspace *wk, obj o, uint32_t depth)
{
	if (depth > 16) {
		return make_str(wk, "...");
	}

	enum obj_type t = get_obj_type(wk, o);

	if (t == obj_typeinfo) {
		obj str = typechecking_type_to_str(wk, get_obj_typeinfo(wk, o)->type);
		return str;
	}

	obj str = make_str(wk, obj_type_to_s(t));

	switch (t) {
	case obj_dict:
	case obj_array: {
		struct obj_type_to_typestr_ctx ctx = { .depth = depth + 1 };
		ctx.arr = make_obj(wk, obj_array);

		if (t == obj_dict) {
			obj_dict_foreach(wk, o, &ctx, obj_type_to_typestr_dict_iter);
		} else {
			obj_array_foreach(wk, o, &ctx, obj_type_to_typestr_arr_iter);
		}

		obj sorted;
		obj_array_sort(wk, NULL, ctx.arr, obj_array_sort_by_str, &sorted);

		obj subtypestr;
		obj_array_join(wk, false, sorted, make_str(wk, "|"), &subtypestr);
		str_appf(wk, &str, "[%s]", get_cstr(wk, subtypestr));
		break;
	}
	default: break;
	}

	return str;
}

obj
obj_type_to_typestr(struct workspace *wk, obj o)
{
	return _obj_type_to_typestr(wk, o, 0);
}

const char *
obj_typestr(struct workspace *wk, obj o)
{
	return get_cstr(wk, obj_type_to_typestr(wk, o));
}

/*
 * ----------------------------------------------------------------------------
 * typechecking
 * ----------------------------------------------------------------------------
 */

type_tag
get_obj_typechecking_type(struct workspace *wk, obj got_obj)
{
	enum obj_type t = get_obj_type(wk, got_obj);
	if (t == obj_typeinfo) {
		return get_obj_typeinfo(wk, got_obj)->type;
	} else {
		return obj_type_to_tc_type(t);
	}
}

struct typecheck_nested_type_ctx {
	type_tag type;
};

static bool typecheck_complex_type(struct workspace *wk, obj got_obj, type_tag got_type, type_tag type);

static enum iteration_result
typecheck_nested_type_arr_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct typecheck_nested_type_ctx *ctx = _ctx;

	if (!typecheck_complex_type(wk, v, get_obj_typechecking_type(wk, v), ctx->type)) {
		return ir_err;
	}

	return ir_cont;
}

static enum iteration_result
typecheck_nested_type_dict_iter(struct workspace *wk, void *_ctx, obj _k, obj v)
{
	return typecheck_nested_type_arr_iter(wk, _ctx, v);
}

static bool
typecheck_complex_type(struct workspace *wk, obj got_obj, type_tag got_type, type_tag type)
{
	// L("typechecking 0%016llx (%s) vs 0%016llx (%s)",
	// 	got_type,
	// 	typechecking_type_to_s(wk, got_type),
	// 	type,
	// 	typechecking_type_to_s(wk, type));

	if (!(type & TYPE_TAG_COMPLEX)) {
		if (got_type & TYPE_TAG_COMPLEX) {
			got_type = flatten_type(wk, got_type);
		}

		got_type &= ~obj_typechecking_type_tag;

		if (!got_type && ((type & TYPE_TAG_ALLOW_NULL) || !(type & ~TYPE_TAG_MASK))) {
			return true;
		}

		if (!got_type && type == tc_func) {
			assert(false);
		}

		type |= tc_disabler; // always allow disabler type
		type &= ~(obj_typechecking_type_tag | TYPE_TAG_ALLOW_NULL);

		assert(!(got_type & TYPE_TAG_MASK));
		assert(!(type & TYPE_TAG_MASK));

		return (!got_type && !type) || (got_type & type);
	}

	uint32_t idx = COMPLEX_TYPE_INDEX(type);
	enum complex_type ct = COMPLEX_TYPE_TYPE(type);

	if (ct == complex_type_preset) {
		return typecheck_complex_type(wk, got_obj, got_type, complex_type_preset_get(wk, idx));
	} else if (ct == complex_type_enum) {
		return typecheck_complex_type(wk, got_obj, got_type, tc_string);
	}

	struct bucket_arr *typeinfo_arr = &wk->vm.objects.obj_aos[obj_typeinfo - _obj_aos_start];
	struct obj_typeinfo *ti = bucket_arr_get(typeinfo_arr, idx);

	switch (ct) {
	case complex_type_or: {
		return typecheck_complex_type(wk, got_obj, got_type, ti->type)
		       || typecheck_complex_type(wk, got_obj, got_type, ti->subtype);
	}
	case complex_type_nested: {
		if (!typecheck_complex_type(wk, got_obj, got_type, ti->type)) {
			return false;
		}

		if (get_obj_type(wk, got_obj) == obj_typeinfo) {
			// currently typeinfo types don't contain nested type
			// information
			return true;
		}

		struct typecheck_nested_type_ctx ctx = {
			.type = ti->subtype,
		};

		if (ti->type == tc_array) {
			return obj_array_foreach(wk, got_obj, &ctx, typecheck_nested_type_arr_iter);
		} else if (ti->type == tc_dict) {
			return obj_dict_foreach(wk, got_obj, &ctx, typecheck_nested_type_dict_iter);
		}
		break;
	}
	case complex_type_enum:
	case complex_type_preset: break;
	}

	UNREACHABLE_RETURN;
}

bool
typecheck_custom(struct workspace *wk, uint32_t ip, obj got_obj, type_tag type, const char *fmt)
{
	type_tag got_type = get_obj_typechecking_type(wk, got_obj);

	if (!(type & obj_typechecking_type_tag)) {
		type = obj_type_to_tc_type(type);
	}

	if (!typecheck_complex_type(wk, got_obj, got_type, type)) {
		if (fmt) {
			vm_error_at(wk,
				ip,
				fmt,
				typechecking_type_to_s(wk, type),
				get_cstr(wk, obj_type_to_typestr(wk, got_obj)));
		}
		return false;
	}

	return true;
}

bool
typecheck(struct workspace *wk, uint32_t ip, obj obj_id, type_tag type)
{
	return typecheck_custom(wk, ip, obj_id, type, "expected type %s, got %s");
}

bool
typecheck_simple_err(struct workspace *wk, obj o, type_tag type)
{
	type_tag got = get_obj_type(wk, o);

	if (got != type) {
		LOG_E("expected type %s, got %s", obj_type_to_s(type), obj_type_to_s(got));
		return false;
	}

	return true;
}

bool
typecheck_typeinfo(struct workspace *wk, obj v, type_tag t)
{
	return get_obj_type(wk, v) == obj_typeinfo
	       && ((flatten_type(wk, get_obj_typeinfo(wk, v)->type) & t) & ~TYPE_TAG_MASK);
}

bool
bounds_adjust(uint32_t len, int64_t *i)
{
	if (*i < 0) {
		*i += len;
	}

	return *i < len;
}

bool
boundscheck(struct workspace *wk, uint32_t ip, uint32_t len, int64_t *i)
{
	if (!bounds_adjust(len, i)) {
		vm_error_at(wk, ip, "index %" PRId64 " out of bounds", *i);
		return false;
	}

	return true;
}

bool
rangecheck(struct workspace *wk, uint32_t ip, int64_t min, int64_t max, int64_t n)
{
	if (n < min || n > max) {
		vm_error_at(wk, ip, "number %" PRId64 " out of bounds (%" PRId64 ", %" PRId64 ")", n, min, max);
		return false;
	}

	return true;
}

bool
type_tags_eql(struct workspace *wk, type_tag a, type_tag b)
{
	if (!(a & TYPE_TAG_COMPLEX)) {
		return a == b;
	}

	if (!(b & TYPE_TAG_COMPLEX)) {
		return false;
	}

	enum complex_type a_ct = COMPLEX_TYPE_TYPE(a), b_ct = COMPLEX_TYPE_TYPE(b);

	if (a_ct != b_ct) {
		return false;
	}

	uint32_t a_idx = COMPLEX_TYPE_INDEX(a), b_idx = COMPLEX_TYPE_INDEX(b);

	if (a_idx == b_idx) {
		return true;
	}

	struct bucket_arr *typeinfo_arr = &wk->vm.objects.obj_aos[obj_typeinfo - _obj_aos_start];
	struct obj_typeinfo *a_ti = bucket_arr_get(typeinfo_arr, a_idx), *b_ti = bucket_arr_get(typeinfo_arr, b_idx);

	return type_tags_eql(wk, a_ti->type, b_ti->type) && type_tags_eql(wk, a_ti->subtype, b_ti->subtype);
}

type_tag
flatten_type_flags(struct workspace *wk, type_tag t, enum flatten_type_flag flags)
{
	t &= ~TYPE_TAG_ALLOW_NULL;

	if (!(t & TYPE_TAG_COMPLEX)) {
		t &= ~TYPE_TAG_GLOB;

		if (t & TYPE_TAG_LISTIFY) {
			t = tc_array;
		}
		return t;
	}

	uint32_t idx = COMPLEX_TYPE_INDEX(t);
	enum complex_type ct = COMPLEX_TYPE_TYPE(t);

	if (ct == complex_type_preset) {
		return flatten_type(wk, complex_type_preset_get(wk, idx));
	} else if (ct == complex_type_enum) {
		if (flags & flatten_type_flag_keep_enums) {
			return t;
		} else {
			return tc_string;
		}
	}

	struct bucket_arr *typeinfo_arr = &wk->vm.objects.obj_aos[obj_typeinfo - _obj_aos_start];
	struct obj_typeinfo *ti = bucket_arr_get(typeinfo_arr, idx);

	switch (ct) {
	case complex_type_or: return flatten_type(wk, ti->type) | flatten_type(wk, ti->subtype);
	case complex_type_nested: return flatten_type(wk, ti->type);
	case complex_type_enum:
	case complex_type_preset: break;
	}

	UNREACHABLE_RETURN;
}

type_tag
flatten_type(struct workspace *wk, type_tag t)
{
	return flatten_type_flags(wk, t, 0);
}


type_tag
complex_type_enum_get_(struct workspace *wk, const char *n)
{
	obj values = vm_enum_values_(wk, n);
	return COMPLEX_TYPE(values, complex_type_enum);
}

type_tag
complex_type_preset_get(struct workspace *wk, enum complex_type_preset t)
{
	obj n;
	if (obj_dict_geti(wk, wk->vm.objects.complex_types, (uint32_t)t, &n)) {
		return get_obj_number(wk, n);
	}

	type_tag tag = 0;

	switch (t) {
	case tc_cx_options_dict_or_list:
		tag = make_complex_type(wk,
			complex_type_or,
			make_complex_type(wk,
				complex_type_or,
				make_complex_type(wk, complex_type_nested, tc_array, tc_string),
				tc_string),
			make_complex_type(
				wk, complex_type_nested, tc_dict, tc_string | tc_number | tc_bool | tc_array));
		break;
	case tc_cx_options_deprecated_kw:
		tag = make_complex_type(wk,
			complex_type_or,
			tc_string | tc_bool,
			make_complex_type(wk,
				complex_type_or,
				make_complex_type(wk, complex_type_nested, tc_dict, tc_string),
				make_complex_type(wk, complex_type_nested, tc_array, tc_string)));
		break;
	case tc_cx_list_of_number: {
		tag = make_complex_type(wk, complex_type_nested, tc_array, tc_number);
		break;
	}
	case tc_cx_dict_of_str: {
		tag = make_complex_type(wk, complex_type_nested, tc_dict, tc_string);
		break;
	}
	case tc_cx_override_find_program: {
		tag = make_complex_type(wk,
			complex_type_or,
			tc_file | tc_external_program | tc_build_target | tc_custom_target | tc_python_installation,
			make_complex_type(wk, complex_type_nested, tc_array, tc_string));
		break;
	}
	case tc_cx_toolchain_overrides: {
		tag = make_complex_type(wk,
			complex_type_nested,
			tc_dict,
			make_complex_type(wk,
				complex_type_or,
				tc_capture,
				make_complex_type(wk,
					complex_type_or,
					tc_bool,
					make_complex_type(wk, complex_type_nested, tc_array, tc_string))));
		break;
	}
	default: UNREACHABLE;
	}

	obj_dict_seti(wk, wk->vm.objects.complex_types, (uint32_t)t, make_number(wk, tag));
	return tag;
}

static bool
typecheck_capture_arg(struct workspace *wk, type_tag a, type_tag b)
{
	bool a_complex = a & TYPE_TAG_COMPLEX, b_complex = b & TYPE_TAG_COMPLEX;
	type_tag tmp;
	if (a_complex && !b_complex) {
		tmp = a;
		a = b;
		b = tmp;
		a_complex = false;
		b_complex = true;
	}

	// make tc_string compatible with enum types
	if (!a_complex && b_complex && a == tc_string) {
		enum complex_type b_ct = COMPLEX_TYPE_TYPE(b);
		if (b_ct == complex_type_enum) {
			return true;
		}
	}

	return type_tags_eql(wk, a, b);
}

void
typecheck_capture_type_to_s(struct workspace *wk,
	struct tstr *res,
	const struct typecheck_capture_sig *sig)
{
	obj expected = make_obj(wk, obj_array);
	if (sig->an) {
		for (uint32_t i = 0; sig->an[i].type != ARG_TYPE_NULL; ++i) {
			obj_array_push(wk, expected, typechecking_type_to_str(wk, sig->an[i].type));
		}
	}

	obj joined;
	obj_array_join(wk, false, expected, make_str(wk, ", "), &joined);

	obj_asprintf(wk, res, "(%#o) -> %s", joined, typechecking_type_to_s(wk, sig->return_type));
}

bool
typecheck_capture(struct workspace *wk,
	uint32_t ip,
	obj v,
	const struct typecheck_capture_sig *sig,
	const char *name)
{
	if (!typecheck(wk, ip, v, obj_capture)) {
		return false;
	}

	struct obj_func *fn = get_obj_capture(wk, v)->func;

	uint32_t i;
	for (i = 0; i < fn->nargs; ++i) {
		if (!sig->an || sig->an[i].type == ARG_TYPE_NULL) {
			// too many posargs
			goto type_err;
		}

		if (!typecheck_capture_arg(wk, sig->an[i].type, fn->an[i].type)) {
			// posargs type mismatch
			goto type_err;
		}
	}

	if (sig->an[i].type != ARG_TYPE_NULL) {
		// too few posargs
		goto type_err;
	}

	if (sig->akw) {
		vm_error_at(wk, ip, "kwarg typechecking not currently supported");
		return false;
	} else if (fn->nkwargs) {
		// no kwargs accepted
		goto type_err;
	}

	if (!type_tags_eql(wk, sig->return_type, fn->return_type)) {
		// return type mistmatch
		goto type_err;
	}

	return true;

type_err: {
	TSTR(buf);
	typecheck_capture_type_to_s(wk, &buf, sig);

	vm_error_at(wk,
		ip,
		"function %s has an invalid signature, expected signature: %s",
		fn->name ? fn->name : name,
		buf.buf);
	return false;
}
}
