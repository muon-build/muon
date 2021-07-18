#include "posix.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "functions/common.h"
#include "functions/default/options.h"
#include "interpreter.h"
#include "log.h"

enum build_option_type {
	op_string,
	op_boolean,
	op_combo,
	op_integer,
	op_array,
	op_feature,
	build_option_type_count,
};

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
		if (strcmp(build_option_type_name[type], wk_objstr(wk, name)) == 0) {
			*res = type;
			return true;
		}
	}

	interp_error(wk, node, "invalid option type '%s'", wk_objstr(wk, name));
	return false;
}

static bool
subproj_name_matches(struct workspace *wk, uint32_t subproj_name, const char *test)
{
	const char *name = wk_str(wk, subproj_name);

	if (test) {
		return name && strcmp(test, name) == 0;
	} else {
		return !name;
	}

}

#define BUF_SIZE 2048

static const char *
option_override_to_s(struct workspace *wk, struct option_override *oo)
{
	static char buf[BUF_SIZE + 1] = { 0 };
	char buf1[BUF_SIZE / 2];

	const char *val;

	if (oo->obj_value) {
		if (obj_to_s(wk, oo->val, buf1, BUF_SIZE / 2)) {
			val = buf1;
		} else {
			val = "<object>";
		}
	} else {
		val = wk_str(wk, oo->val);
	}

	snprintf(buf, BUF_SIZE, "%s%s%s=%s",
		oo->proj ? wk_str(wk, oo->proj) : "",
		oo->proj ? ":" : "",
		wk_str(wk, oo->name),
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

		if (subproj_name_matches(wk, current_project(wk)->subproject_name, wk_str(wk, oo->proj))) {
			bool found;
			uint32_t res;
			char *name = wk_str(wk, oo->name);

			if (!obj_dict_index_strn(wk, current_project(wk)->opts, name, strlen(name), &res, &found)) {
				return false;
			} else if (!found) {
				LOG_W(log_interp, "invalid option: '%s'", option_override_to_s(wk, oo));
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

			if (strcmp(wk_str(wk, proj->subproject_name), wk_str(wk, oo->proj)) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			LOG_W(log_interp, "invalid option: '%s' (no such subproject)", option_override_to_s(wk, oo));
			ret = false;
		}
	}

	return ret;
}

static bool
find_option_override(struct workspace *wk, const char *key, struct option_override **oo)
{
	uint32_t i;
	for (i = 0; i < wk->option_overrides.len; ++i) {
		*oo = darr_get(&wk->option_overrides, i);

		if (subproj_name_matches(wk, current_project(wk)->subproject_name, wk_str(wk, (*oo)->proj))
		    && strcmp(key, wk_str(wk, (*oo)->name)) == 0) {
			return true;
		}
	}

	return false;
}

static bool
coerce_feature_opt(struct workspace *wk, uint32_t node, const char *val, uint32_t *obj_id)
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

	make_obj(wk, obj_id, obj_feature_opt)->dat.feature_opt.state = f;
	return true;
}

static bool
coerce_option_override(struct workspace *wk, uint32_t node, enum build_option_type type, const char *val, uint32_t *obj_id)
{
	switch (type) {
	case op_combo:
	case op_string: make_obj(wk, obj_id, obj_string)->dat.str = wk_str_push(wk, val);
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

		make_obj(wk, obj_id, obj_bool)->dat.boolean = b;
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

		make_obj(wk, obj_id, obj_number)->dat.num = num;
		break;
	}
	case op_array: {
		interp_error(wk, node, "TODO '%s'.split(',') :^)", val);
		return false;
		break;
	}
	case op_feature:
		return coerce_feature_opt(wk, node, val, obj_id);
	default:
		assert(false && "unreachable");
	}

	return true;
}


struct check_array_opt_ctx {
	struct args_kw *choices;
};
enum iteration_result
check_array_opt_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct check_array_opt_ctx *ctx = _ctx;

	bool res;
	if (!obj_array_in(wk, val, ctx->choices->val, &res)) {
		return ir_err;
	} else if (!res) {
		interp_error(wk, ctx->choices->node, "array element '%s' is not a valid choice", wk_objstr(wk, val));
		return ir_err;
	}

	return ir_cont;
}

static bool
typecheck_opt(struct workspace *wk, uint32_t err_node, uint32_t val, enum build_option_type type, uint32_t *res)
{
	enum obj_type expected_type;

	if (type == op_feature) {
		if (!typecheck(wk, err_node, val, obj_string)) {
			return false;
		} else if (!coerce_feature_opt(wk, err_node, wk_objstr(wk, val), res)) {
			return false;
		}

		return true;
	}

	switch (type) {
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
check_superproject_option(struct workspace *wk, uint32_t node, enum obj_type type, uint32_t name, uint32_t *ret)
{
	uint32_t val;
	if (!get_option(wk, darr_get(&wk->projects, 0), wk_objstr(wk, name), &val)) {
		return true;
	}

	if (!typecheck(wk, node, val, type)) {
		return false;
	}

	*ret = val;
	return true;
}

bool
func_option(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
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
			assert(false && "unreachble");
		}
	}

	uint32_t val = 0;
	if (akw[kw_value].set) {
		if (!typecheck_opt(wk, akw[kw_value].node, akw[kw_value].val, type, &val)) {
			return false;
		}
	} else {
		switch (type) {
		case op_string: make_obj(wk, &val, obj_string)->dat.str = wk_str_push(wk, "");
			break;
		case op_boolean: make_obj(wk, &val, obj_bool)->dat.boolean = true;
			break;
		case op_combo:
			if (!get_obj(wk, akw[kw_choices].val)->dat.arr.len) {
				interp_error(wk, akw[kw_choices].node, "unable to set default for combo option with no choices");
				return false;
			}

			if (!obj_array_index(wk, akw[kw_choices].val, 0, &val)) {
				return false;
			}
			break;
		case op_array: val = akw[kw_choices].val;
			break;
		case op_feature:
			make_obj(wk, &val, obj_feature_opt)->dat.feature_opt.state = feature_opt_auto;
			break;
		default:
			assert(false && "unreachable");
			return false;
		}
	}

	struct option_override *oo;
	if (find_option_override(wk, wk_objstr(wk, an[0].val), &oo)) {
		if (oo->obj_value) {
			if (!typecheck_opt(wk, akw[kw_type].node, oo->val, type, &val)) {
				return false;
			}
		} else if (!coerce_option_override(wk, akw[kw_type].node, type, wk_str(wk, oo->val), &val)) {
			return false;
		}
	} else if (wk->cur_project != 0 && akw[kw_yield].set && get_obj(wk, akw[kw_yield].val)->dat.boolean) {
		if (!check_superproject_option(wk, akw[kw_type].node, get_obj(wk, val)->type, an[0].val, &val)) {
			return false;
		}
	}

	switch (type) {
	case op_combo: {
		bool res;
		if (!obj_array_in(wk, val, akw[kw_choices].val, &res)) {
			return false;
		} else if (!res) {
			interp_error(wk, akw[kw_choices].node, "'%s' is not valid for '%s'",
				wk_objstr(wk, val), wk_objstr(wk, an[0].val));
			return false;
		}
		break;
	}
	case op_integer: {
		if (akw[kw_max].set && get_obj(wk, val)->dat.num > get_obj(wk, akw[kw_max].val)->dat.num) {
			interp_error(wk, akw[kw_max].node, "value %ld is too large", (intmax_t)get_obj(wk, val)->dat.num);
			return false;
		}

		if (akw[kw_min].set && get_obj(wk, val)->dat.num < get_obj(wk, akw[kw_min].val)->dat.num) {
			interp_error(wk, akw[kw_min].node, "value %ld is too small", (intmax_t)get_obj(wk, val)->dat.num);
			return false;
		}
		break;
	}
	case op_array: {
		if (akw[kw_choices].set) {
			obj_array_foreach(wk, val, &(struct check_array_opt_ctx) { &akw[kw_choices] },
				check_array_opt_iter);
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

	bool res;
	if (!obj_dict_in(wk, an[0].val, current_project(wk)->opts, &res)) {
		return false;
	} else if (res) {
		interp_error(wk, an[0].node, "duplicate option name");
		return false;
	}
	obj_dict_set(wk, current_project(wk)->opts, an[0].val, val);

	return true;
}

bool
func_get_option(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	bool res;
	if (!obj_dict_index(wk, current_project(wk)->opts, an[0].val, obj, &res)) {
		return false;
	} else if (!res) {
		interp_error(wk, an[0].node, "undefined option");
		return false;
	}

	return true;
}

bool
get_option(struct workspace *wk, struct project *proj, const char *name, uint32_t *obj)
{
	bool res;
	if (!obj_dict_index_strn(wk, proj->opts, name, strlen(name), obj, &res)) {
		return false;
	} else if (!res) {
		return false;
	}

	return true;
}

bool
set_default_options(struct workspace *wk)
{
	uint32_t obj;
	return eval_str(wk,
		"option('default_library', yield: true, type: 'string', value: 'static')\n"
		"option('debug', yield: true, type: 'boolean', value: true)\n"
		"option('buildtype', yield: true, type: 'combo', value: 'debugoptimized',\n"
		"\tchoices: ['plain', 'debug', 'debugoptimized', 'release', 'minsize', 'custom'])\n"
		"option('optimization', yield: true, type: 'combo', value: 'g',\n"
		"\tchoices: ['0', 'g', '1', '2', '3', 's'])\n"
		"option('warning_level', yield: true, type: 'integer', value: 3,\n"
		"\tmin: 0, max: 3)\n"
		"option('c_std', yield: true, type: 'combo', value: 'none',\n"
		"\tchoices: ['none', 'c89', 'c99', 'c11', 'c17', 'c18', 'c2x', 'gnu89', 'gnu99',"
		" 'gnu11', 'gnu17', 'gnu18', 'gnu2x'])\n"
		"option('prefix', yield: true, type: 'string', value: '/usr/local')\n"
		"option('mandir', yield: true, type: 'string', value: 'share/man')\n"
		"option('datadir', yield: true, type: 'string', value: 'share')\n"

		, &obj);
}
