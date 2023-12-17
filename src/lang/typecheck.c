/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "error.h"
#include "lang/interpreter.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"

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
	struct bucket_arr *typeinfo_arr = &wk->obj_aos[obj_typeinfo - _obj_aos_start];
	uint32_t idx = typeinfo_arr->len;
	bucket_arr_push(typeinfo_arr, &(struct obj_typeinfo) { .type = type, .subtype=subtype });
	return COMPLEX_TYPE(idx, t);
}

/*
 * ----------------------------------------------------------------------------
 *  type to typestr
 * ----------------------------------------------------------------------------
 */

static obj typechecking_type_to_str(struct workspace *wk, type_tag t);

static obj
simple_type_to_arr(struct workspace *wk, type_tag t)
{
	obj expected_types;
	make_obj(wk, &expected_types, obj_array);

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

	uint32_t ot;
	for (ot = 1; ot <= tc_type_count; ++ot) {
		uint32_t tc = obj_type_to_tc_type(ot);
		if ((t & tc) != tc) {
			continue;
		}

		obj_array_push(wk, expected_types, make_str(wk, obj_type_to_s(ot)));
	}

	if (!get_obj_array(wk, expected_types)->len) {
		obj_array_push(wk, expected_types, make_str(wk, "void"));
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

	struct bucket_arr *typeinfo_arr = &wk->obj_aos[obj_typeinfo - _obj_aos_start];
	struct obj_typeinfo *ti = bucket_arr_get(typeinfo_arr, idx);

	obj typestr = typechecking_type_to_str(wk, ti->type);

	if (!ti->subtype) {
		obj res;
		make_obj(wk, &res, obj_array);
		obj_array_push(wk, res, typestr);
		return res;
	}

	switch (ct) {
	case complex_type_or: {
		obj subtype_arr = typechecking_type_to_arr(wk, ti->subtype);
		obj_array_push(wk, subtype_arr, typestr);
		return subtype_arr;
	}
	case complex_type_nested: {
		str_appf(wk, &typestr, "[%s]", typechecking_type_to_s(wk, ti->subtype));

		obj res;
		make_obj(wk, &res, obj_array);
		obj_array_push(wk, res, typestr);
		return res;
	}
	}

	UNREACHABLE_RETURN;
}

static obj
typechecking_type_to_str(struct workspace *wk, type_tag t)
{
	obj typestr;

	const char *modifier = 0;
	if (t & ARG_TYPE_GLOB) {
		t &= ~ARG_TYPE_GLOB;
		modifier = "glob";
	} else if ((t & ARG_TYPE_ARRAY_OF)) {
		t &= ~ARG_TYPE_ARRAY_OF;
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
};

obj obj_type_to_typestr(struct workspace *wk, obj o);

static enum iteration_result
obj_type_to_typestr_arr_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct obj_type_to_typestr_ctx *ctx = _ctx;

	obj s = obj_type_to_typestr(wk, v);

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

obj
obj_type_to_typestr(struct workspace *wk, obj o)
{
	enum obj_type t = get_obj_type(wk, o);
	obj str = make_str(wk, obj_type_to_s(t));

	switch (t) {
	case obj_dict:
	case obj_array: {
		struct obj_type_to_typestr_ctx ctx = { 0 };
		make_obj(wk, &ctx.arr, obj_array);

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
	default:
		break;
	}

	return str;
}

/*
 * ----------------------------------------------------------------------------
 * typechecking
 * ----------------------------------------------------------------------------
 */

static bool
typecheck_typechecking_type(enum obj_type got, type_tag type)
{
	type |= tc_disabler; // always allow disabler type

	type_tag ot;
	for (ot = 1; ot <= tc_type_count; ++ot) {
		type_tag tc = obj_type_to_tc_type(ot);
		if ((type & tc) != tc) {
			continue;
		}

		if (ot == got) {
			return true;
		}
	}
	return false;
}

struct typecheck_nested_type_ctx {
	type_tag type;
};

static bool typecheck_complex_type(struct workspace *wk, obj got, type_tag type);

static enum iteration_result
typecheck_nested_type_arr_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct typecheck_nested_type_ctx *ctx = _ctx;

	if (!typecheck_complex_type(wk, v, ctx->type)) {
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
typecheck_complex_type(struct workspace *wk, obj got, type_tag type)
{
	if (!(type & TYPE_TAG_COMPLEX)) {
		return typecheck_typechecking_type(get_obj_type(wk, got), type);
	}

	uint32_t idx = COMPLEX_TYPE_INDEX(type);
	enum complex_type ct = COMPLEX_TYPE_TYPE(type);

	struct bucket_arr *typeinfo_arr = &wk->obj_aos[obj_typeinfo - _obj_aos_start];
	struct obj_typeinfo *ti = bucket_arr_get(typeinfo_arr, idx);

	switch (ct) {
	case complex_type_or: {
		return typecheck_complex_type(wk, got, ti->type)
			|| typecheck_complex_type(wk, got, ti->subtype);
	}
	case complex_type_nested: {
		if (!typecheck_complex_type(wk, got, ti->type)) {
			return false;
		}

		struct typecheck_nested_type_ctx ctx = {
			.type = ti->subtype,
		};

		if (ti->type == tc_array) {
			return obj_array_foreach(wk, got, &ctx, typecheck_nested_type_arr_iter);
		} else if (ti->type == tc_dict) {
			return obj_dict_foreach(wk, got, &ctx, typecheck_nested_type_dict_iter);
		}
	}
	}

	UNREACHABLE_RETURN;
}

bool
typecheck_custom(struct workspace *wk, uint32_t n_id, obj obj_id, type_tag type, const char *fmt)
{
	enum obj_type got = get_obj_type(wk, obj_id);

	if (!type) {
		return true;
	} else if (got == obj_typeinfo) {
		struct obj_typeinfo *ti = get_obj_typeinfo(wk, obj_id);
		type_tag got = ti->type;
		type_tag t = type;

		if (!(t & obj_typechecking_type_tag)) {
			t = obj_type_to_tc_type(type);
		}

		type_tag ot;
		for (ot = 1; ot <= tc_type_count; ++ot) {
			type_tag tc = obj_type_to_tc_type(ot);

			if ((got & tc) != tc) {
				continue;
			}

			if (typecheck_typechecking_type(ot, t)) {
				return true;
			}
		}

		if (fmt) {
			interp_error(wk, n_id, fmt,
				typechecking_type_to_s(wk, t),
				typechecking_type_to_s(wk, got));
		}
		return false;
	} else if ((type & TYPE_TAG_COMPLEX)) {
		if (!typecheck_complex_type(wk, obj_id, type)) {
			if (fmt) {
				interp_error(wk, n_id, fmt,
					typechecking_type_to_s(wk, type),
					get_cstr(wk, obj_type_to_typestr(wk, obj_id)));
			}
			return false;
		}
	} else if ((type & obj_typechecking_type_tag)) {
		if (!typecheck_typechecking_type(got, type)) {
			if (fmt) {
				interp_error(wk, n_id, fmt,
					typechecking_type_to_s(wk, type),
					obj_type_to_s(got));
			}
			return false;
		}
	} else {
		if (got != type) {
			if (fmt) {
				interp_error(wk, n_id, fmt, obj_type_to_s(type), obj_type_to_s(got));
			}
			return false;
		}
	}

	return true;
}

bool
typecheck(struct workspace *wk, uint32_t n_id, obj obj_id, type_tag type)
{
	return typecheck_custom(wk, n_id, obj_id, type, "expected type %s, got %s");
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
