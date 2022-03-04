#include "posix.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "embedded.h"
#include "functions/common.h"
#include "functions/default/options.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
parse_config_string(struct workspace *wk, char *lhs, struct option_override *oo)
{
	char *rhs = strchr(lhs, '=');
	if (!rhs) {
		LOG_E("expected '=' in config opt '%s'", lhs);
		return false;
	}
	*rhs = 0;
	++rhs;

	char *subproj;

	subproj = lhs;
	if ((lhs = strchr(lhs, ':'))) {
		*lhs = 0;
		++lhs;
	} else {
		lhs = subproj;
		subproj = NULL;
	}

	if (!*lhs) {
		LOG_E("'%s%s=%s' missing option name",
			subproj ? subproj : "", subproj ? ":" : "", rhs);
		return false;
	} else if (subproj && !*subproj) {
		LOG_E("':%s=%s' there is a colon in the option name,"
			"but no subproject was specified", lhs, rhs);
		return false;
	}

	oo->name = make_str(wk, lhs);
	oo->val = make_str(wk, rhs);
	if (subproj) {
		oo->proj = make_str(wk, subproj);
	}

	return true;
}

static bool
build_option_type_from_s(struct workspace *wk, uint32_t node, uint32_t name, enum build_option_type *res)
{
	static const char *build_option_type_name[] = {
		[op_string] = "string",
		[op_boolean] = "boolean",
		[op_combo] = "combo",
		[op_integer] = "integer",
		[op_array] = "array",
		[op_feature] = "feature",
	};

	enum build_option_type type;
	for (type = 0; type < build_option_type_count; ++type) {
		if (strcmp(build_option_type_name[type], get_cstr(wk, name)) == 0) {
			*res = type;
			return true;
		}
	}

	interp_error(wk, node, "invalid option type '%s'", get_cstr(wk, name));
	return false;
}

static bool
subproj_name_matches(struct workspace *wk, const char *name, const char *test)
{
	if (test) {
		return name && strcmp(test, name) == 0;
	} else {
		return !name;
	}

}

static const char *
option_override_to_s(struct workspace *wk, struct option_override *oo)
{
	static char buf[BUF_SIZE_2k + 1] = { 0 };
	char buf1[BUF_SIZE_2k / 2];

	const char *val;

	if (oo->obj_value) {
		obj_to_s(wk, oo->val, buf1, BUF_SIZE_2k / 2);
		val = buf1;
	} else {
		val = get_cstr(wk, oo->val);
	}

	snprintf(buf, BUF_SIZE_2k, "%s%s%s=%s",
		oo->proj ? get_cstr(wk, oo->proj) : "",
		oo->proj ? ":" : "",
		get_cstr(wk, oo->name),
		val
		);

	return buf;
}

bool
check_invalid_option_overrides(struct workspace *wk)
{
	bool ret = true;
	uint32_t i;
	struct option_override *oo;

	for (i = 0; i < wk->option_overrides.len; ++i) {
		oo = darr_get(&wk->option_overrides, i);

		if (subproj_name_matches(wk, get_cstr(wk, current_project(wk)->subproject_name), get_cstr(wk, oo->proj))) {
			obj res;
			const char *name = get_cstr(wk, oo->name);

			if (!obj_dict_index_strn(wk, current_project(wk)->opts, name, strlen(name), &res)) {
				LOG_E("invalid option: '%s'", option_override_to_s(wk, oo));
				ret = false;
			}
		}
	}

	return ret;
}

bool
check_invalid_subproject_option(struct workspace *wk)
{
	uint32_t i, j;
	struct option_override *oo;
	struct project *proj;
	bool found, ret = true;

	for (i = 0; i < wk->option_overrides.len; ++i) {
		oo = darr_get(&wk->option_overrides, i);
		if (!oo->proj) {
			continue;
		}

		found = false;

		for (j = 1; j < wk->projects.len; ++j) {
			proj = darr_get(&wk->projects, j);

			if (strcmp(get_cstr(wk, proj->subproject_name), get_cstr(wk, oo->proj)) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			LOG_E("invalid option: '%s' (no such subproject)", option_override_to_s(wk, oo));
			ret = false;
		}
	}

	return ret;
}

static bool
find_option_override(struct workspace *wk, const char *subproject_name, const char *key, struct option_override **oo)
{
	uint32_t i;
	for (i = 0; i < wk->option_overrides.len; ++i) {
		*oo = darr_get(&wk->option_overrides, i);

		if (subproj_name_matches(wk, subproject_name, get_cstr(wk, (*oo)->proj))
		    && strcmp(key, get_cstr(wk, (*oo)->name)) == 0) {
			return true;
		}
	}

	return false;
}

static bool
coerce_feature_opt(struct workspace *wk, uint32_t node, const char *val, obj *res)
{
	enum feature_opt_state f;

	if (strcmp(val, "auto") == 0) {
		f = feature_opt_auto;
	} else if (strcmp(val, "enabled") == 0) {
		f = feature_opt_enabled;
	} else if (strcmp(val, "disabled") == 0) {
		f = feature_opt_disabled;
	} else {
		interp_error(wk, node, "unable to coerce '%s' into a feature", val);
		return false;
	}

	make_obj(wk, res, obj_feature_opt);
	get_obj_feature_opt(wk, *res)->state = f;
	return true;
}

static bool
coerce_option_override(struct workspace *wk, uint32_t node, enum build_option_type type, const char *val, obj *res)
{
	switch (type) {
	case op_combo:
	case op_string:
		*res = make_str(wk, val);
		break;
	case op_boolean: {
		bool b;
		if (strcmp(val, "true") == 0) {
			b = true;
		} else if (strcmp(val, "false") == 0) {
			b = false;
		} else {
			interp_error(wk, node, "unable to coerce '%s' into a boolean", val);
			return false;
		}

		make_obj(wk, res, obj_bool);
		set_obj_bool(wk, *res, b);
		break;
	}
	case op_integer: {
		int64_t num;
		char *endptr;
		num = strtol(val, &endptr, 10);

		if (!*val || *endptr) {
			interp_error(wk, node, "unable to coerce '%s' into a number", val);
			return false;
		}

		make_obj(wk, res, obj_number);
		set_obj_number(wk, *res, num);
		break;
	}
	case op_array: {
		*res = str_split(wk, &WKSTR(val), &WKSTR(","));
		break;
	}
	case op_feature:
		return coerce_feature_opt(wk, node, val, res);
	default:
		assert(false && "unreachable");
	}

	return true;
}


struct check_array_opt_ctx {
	obj choices;
	uint32_t node;
};

static enum iteration_result
check_array_opt_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct check_array_opt_ctx *ctx = _ctx;

	if (!obj_array_in(wk, ctx->choices, val)) {
		interp_error(wk, ctx->node, "array element %o is not one of %o", val, ctx->choices);
		return ir_err;
	}

	return ir_cont;
}

static bool
typecheck_opt(struct workspace *wk, uint32_t err_node, obj val, enum build_option_type type, obj *res)
{
	enum obj_type expected_type;

	if (type == op_feature && get_obj_type(wk, val) == obj_string) {
		if (!coerce_feature_opt(wk, err_node, get_cstr(wk, val), res)) {
			return false;
		}

		val = *res;
	}

	switch (type) {
	case op_feature: expected_type = obj_feature_opt; break;
	case op_string: expected_type = obj_string; break;
	case op_boolean: expected_type = obj_bool; break;
	case op_combo: expected_type = obj_string; break;
	case op_integer: expected_type = obj_number; break;
	case op_array: expected_type = obj_array; break;
	default:
		assert(false && "unreachable");
		return false;
	}

	if (!typecheck(wk, err_node, val, expected_type)) {
		return false;
	}

	*res = val;
	return true;
}

static bool
check_superproject_option(struct workspace *wk, uint32_t node, enum obj_type type, obj name, obj *ret)
{
	obj val;
	obj opt;
	if (!obj_dict_index(wk, current_project(wk)->opts, name, &opt)) {
		return true;
	}

	struct obj_option *o = get_obj_option(wk, opt);
	val = o->type;

	if (!typecheck_opt(wk, node, val, o->type, ret)) {
		return false;
	}

	*ret = val;
	return true;
}

static bool
option_set(struct workspace *wk, uint32_t node, obj opt, obj new_val)
{
	struct obj_option *o = get_obj_option(wk, opt);

	obj val;
	if (!typecheck_opt(wk, node, new_val, o->type, &val)) {
		return false;
	}

	switch (o->type) {
	case op_combo: {
		if (!obj_array_in(wk, o->choices, val)) {
			interp_error(wk, node, "'%o' is not one of %o", val, o->choices);
			return false;
		}
		break;
	}
	case op_integer: {
		int64_t num = get_obj_number(wk, val);

		if ((o->max && num > get_obj_number(wk, o->max))
		    || (o->min && num < get_obj_number(wk, o->min)) ) {
			interp_error(wk, node, "value %" PRId64 " is out of range (%" PRId64 "..%" PRId64 ")",
				get_obj_number(wk, val),
				(o->min ? get_obj_number(wk, o->min) : INT64_MIN),
				(o->max ? get_obj_number(wk, o->max) : INT64_MAX)
				);
			return false;
		}
		break;
	}
	case op_array: {
		if (o->choices) {
			if (!obj_array_foreach(wk, val, &(struct check_array_opt_ctx) {
					.choices = o->choices,
					.node = node,
				}, check_array_opt_iter)) {
				return false;
			}
		}
		break;
	}
	case op_string:
	case op_feature:
	case op_boolean:
		break;
	default:
		assert(false && "unreachable");
		return false;
	}

	o->val = val;
	return true;
}

bool
func_option(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_type,
		kw_value,
		kw_description,
		kw_choices,
		kw_max,
		kw_min,
		kw_yield,
		kwargs_count
	};
	struct args_kw akw[] = {
		[kw_type] = { "type", obj_string },
		[kw_value] = { "value", },
		[kw_description] = { "description", obj_string },
		[kw_choices] = { "choices", obj_array },
		[kw_max] = { "max", obj_number },
		[kw_min] = { "min", obj_number },
		[kw_yield] = { "yield", obj_bool },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (!akw[kw_type].set) {
		interp_error(wk, args_node, "missing required keyword 'type'");
		return false;
	}

	enum build_option_type type;
	if (!build_option_type_from_s(wk, akw[kw_type].node, akw[kw_type].val, &type)) {
		return false;
	}

	enum keyword_req {
		kw_opt, // optional
		kw_req, // required
		kw_inv, // invalid
	};

	static const enum keyword_req keyword_validity[build_option_type_count][kwargs_count] = {
		/*               kw_type, kw_value, kw_description, kw_choices, kw_max, kw_min, kw_yield */
		[op_string]  = { kw_req,  kw_opt,   kw_opt,         kw_inv,     kw_inv, kw_inv, kw_opt, },
		[op_boolean] = { kw_req,  kw_opt,   kw_opt,         kw_inv,     kw_inv, kw_inv, kw_opt, },
		[op_combo]   = { kw_req,  kw_opt,   kw_opt,         kw_req,     kw_inv, kw_inv, kw_opt, },
		[op_integer] = { kw_req,  kw_req,   kw_opt,         kw_inv,     kw_opt, kw_opt, kw_opt, },
		[op_array]   = { kw_req,  kw_opt,   kw_opt,         kw_opt,     kw_inv, kw_inv, kw_opt, },
		[op_feature] = { kw_req,  kw_opt,   kw_opt,         kw_inv,     kw_inv, kw_inv, kw_opt, },
	};

	uint32_t i;
	for (i = 0; i < kwargs_count; ++i) {
		switch (keyword_validity[type][i]) {
		case kw_opt:
			break;
		case kw_inv:
			if (akw[i].set) {
				interp_error(wk, akw[i].node, "invalid keyword for option type");
				return false;
			}
			break;
		case kw_req:
			if (!akw[i].set) {
				interp_error(wk, args_node, "missing keyword '%s' for option type", akw[i].key);
				return false;
			}
			break;
		default:
			assert(false && "unreachable");
		}
	}

	obj val = 0;

	if (akw[kw_value].set) {
		val = akw[kw_value].val;
	} else {
		switch (type) {
		case op_string:
			val = make_str(wk, "");
			break;
		case op_boolean:
			make_obj(wk, &val, obj_bool);
			set_obj_bool(wk, val, true);
			break;
		case op_combo:
			if (!get_obj_array(wk, akw[kw_choices].val)->len) {
				interp_error(wk, akw[kw_choices].node, "combo option with no choices");
				return false;
			}

			obj_array_index(wk, akw[kw_choices].val, 0, &val);
			break;
		case op_array:
			if (akw[kw_choices].set) {
				val = akw[kw_choices].val;
			} else {
				make_obj(wk, &val, obj_array);
			}
			break;
		case op_feature:
			make_obj(wk, &val, obj_feature_opt);
			get_obj_feature_opt(wk, val)->state = feature_opt_auto;
			break;
		default:
			assert(false && "unreachable");
			return false;
		}
	}

	struct option_override *oo;

	if (find_option_override(wk, get_cstr(wk, current_project(wk)->subproject_name), get_cstr(wk, an[0].val), &oo)) {
		if (oo->obj_value) {
			if (!typecheck_opt(wk, akw[kw_type].node, oo->val, type, &val)) {
				return false;
			}
		} else if (!coerce_option_override(wk, akw[kw_type].node, type, get_cstr(wk, oo->val), &val)) {
			return false;
		}
	} else if (wk->cur_project != 0 && akw[kw_yield].set && get_obj_bool(wk, akw[kw_yield].val)) {
		if (!check_superproject_option(wk, akw[kw_type].node, get_obj_type(wk, val), an[0].val, &val)) {
			return false;
		}
	}

	if (obj_dict_in(wk, current_project(wk)->opts, an[0].val)) {
		interp_error(wk, an[0].node, "duplicate option name");
		return false;
	}

	obj opt;
	make_obj(wk, &opt, obj_option);
	struct obj_option *o = get_obj_option(wk, opt);
	o->type = type;
	o->min = akw[kw_min].val;
	o->max = akw[kw_max].val;
	o->choices = akw[kw_choices].val;

	if (!option_set(wk, args_node, opt, val)) {
		return false;
	}

	obj_dict_set(wk, current_project(wk)->opts, an[0].val, opt);

	return true;
}

void
get_option(struct workspace *wk, const struct project *proj, const char *name, obj *res)
{
	obj opt;
	if (!obj_dict_index_strn(wk, proj->opts, name, strlen(name), &opt)) {
		LOG_E("attempted to get unknown option '%s'", name);
		assert(false);
	}

	struct obj_option *o = get_obj_option(wk, opt);
	*res = o->val;
}

enum wrap_mode
get_option_wrap_mode(struct workspace *wk)
{
	obj opt;
	get_option(wk, current_project(wk), "wrap_mode", &opt);

	const char *s = get_cstr(wk, opt);

	const char *names[] = {
		[wrap_mode_nopromote] = "nopromote",
		[wrap_mode_nodownload] = "nodownload",
		[wrap_mode_nofallback] = "nofallback",
		[wrap_mode_forcefallback] = "forcefallback",
		NULL,
	};

	uint32_t i;
	for (i = 0; names[i]; ++i) {
		if (strcmp(names[i], s) == 0) {
			return i;
		}
	}

	assert(false && "invalid wrap_mode set");
	return 0;
}

bool
func_get_option(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	obj opt;
	if (!obj_dict_index(wk, current_project(wk)->opts, an[0].val, &opt)) {
		interp_error(wk, an[0].node, "undefined option");
		return false;
	}

	struct obj_option *o = get_obj_option(wk, opt);
	*res = o->val;
	return true;
}

bool
set_builtin_options(struct workspace *wk)
{
	const char *fallback_options =
		"option('default_library', type: 'string', value: 'static')\n"
		"option('buildtype', type: 'string', value: 'debugoptimized')\n"
		"option('warning_level', type: 'string', value: '3')\n"
		"option('c_std', type: 'string', value: 'c99')\n"
		"option('prefix', type: 'string', value: '/usr/local')\n"
		"option('bindir', type: 'string', value: 'bin')\n"
		"option('mandir', type: 'string', value: 'share/man')\n"
		"option('wrap_mode', type: 'string', value: 'nopromote')\n"
	;

	const char *opts;
	if (!(opts = embedded_get("builtin_options.meson"))) {
		opts = fallback_options;
	}

	uint32_t obj;
	return eval_str(wk, opts, &obj);
}

bool
parse_and_set_cmdline_option(struct workspace *wk, char *lhs)
{
	struct option_override oo = { 0 };
	if (!parse_config_string(wk, lhs, &oo)) {
		return false;
	}

	darr_push(&wk->option_overrides, &oo);
	return true;
}

struct parse_and_set_default_options_ctx {
	uint32_t node;
	obj project_name;
	bool subproject;
};

static enum iteration_result
parse_and_set_default_options_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct parse_and_set_default_options_ctx *ctx = _ctx;

	const struct str *ss = get_str(wk, v);
	if (str_has_null(ss)) {
		interp_error(wk, ctx->node, "invalid option string");
		return ir_err;
	}

	char *s = (char *)ss->s;
	struct option_override oo = { 0 }, *other_oo;
	if (!parse_config_string(wk, s, &oo)) {
		interp_error(wk, ctx->node, "invalid option string");
		return ir_err;
	}

	if (!oo.proj) {
		oo.proj = ctx->project_name;
	}

	if (find_option_override(wk, get_cstr(wk, oo.proj), get_cstr(wk, oo.name), &other_oo)) {
		/* ignore, already set */
		L("ignoring default_options value %s as it was already set on the commandline", option_override_to_s(wk, &oo));
		return ir_cont;
	}

	L("setting default_option %s", option_override_to_s(wk, &oo));

	if (ctx->subproject) {
		darr_push(&wk->option_overrides, &oo);
		return ir_cont;
	}

	obj opt;
	if (obj_dict_index(wk, current_project(wk)->opts, oo.name, &opt)) {
		struct obj_option *o = get_obj_option(wk, opt);

		obj val;
		if (!coerce_option_override(wk, ctx->node, o->type, get_cstr(wk, oo.val), &val)) {
			return ir_err;
		}

		if (!option_set(wk, ctx->node, opt, val)) {
			return ir_err;
		}
		return ir_cont;
	} else {
		LOG_E("invalid option: '%s'", option_override_to_s(wk, &oo));
		return ir_err;
	}
}

bool
parse_and_set_default_options(struct workspace *wk, uint32_t err_node, obj arr, obj project_name, bool is_subproject)
{
	struct parse_and_set_default_options_ctx ctx = {
		.node = err_node,
		.project_name = project_name,
		.subproject = is_subproject,
	};

	if (!obj_array_foreach(wk, arr, &ctx, parse_and_set_default_options_iter)) {
		return false;
	}

	return true;
}
