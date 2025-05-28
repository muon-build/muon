/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "error.h"
#include "functions/kernel/options.h"
#include "lang/analyze.h"
#include "lang/typecheck.h"
#include "options.h"
#include "platform/assert.h"

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
		[op_shell_array] = "shell_array",
	};

	enum build_option_type type;
	for (type = 0; type < build_option_type_count; ++type) {
		if (type == op_shell_array && !initializing_builtin_options) {
			continue;
		}

		if (strcmp(build_option_type_name[type], get_cstr(wk, name)) == 0) {
			*res = type;
			return true;
		}
	}

	vm_error_at(wk, node, "invalid option type '%s'", get_cstr(wk, name));
	return false;
}

static bool
validate_option_name(struct workspace *wk, uint32_t err_node, obj name)
{
	uint32_t i;
	const struct str *s = get_str(wk, name);
	for (i = 0; i < s->len; ++i) {
		if (('a' <= s->s[i] && s->s[i] <= 'z') || ('A' <= s->s[i] && s->s[i] <= 'Z')
			|| ('0' <= s->s[i] && s->s[i] <= '9') || (s->s[i] == '-') || (s->s[i] == '_')) {
			continue;
		}

		vm_error_at(wk, err_node, "option name may not contain '%c'", s->s[i]);
		return false;
	}

	return true;
}

bool
func_option(struct workspace *wk, obj self, obj *res)
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
		kw_deprecated,
		kwargs_count,
		kw_kind = kwargs_count,
	};

	struct args_kw akw[] = { [kw_type] = { "type", obj_string },
		[kw_value] = { "value", tc_any },
		[kw_description] = { "description", obj_string },
		[kw_choices] = { "choices", obj_array },
		[kw_max] = { "max", obj_number },
		[kw_min] = { "min", obj_number },
		[kw_yield] = { "yield", obj_bool },
		[kw_deprecated] = { "deprecated", COMPLEX_TYPE_PRESET(tc_cx_options_deprecated_kw) },
		[kw_kind] = { 0 },
		0 };

	if (initializing_builtin_options) {
		akw[kw_kind] = (struct args_kw){ "kind", tc_string };
	}

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (!akw[kw_type].set) {
		vm_error(wk, "missing required keyword 'type'");
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
		/*               kw_type, kw_value, kw_description, kw_choices, kw_max, kw_min, kw_yield, kw_deprecated */
		[op_string]  = { kw_req,  kw_opt,   kw_opt,         kw_inv,     kw_inv, kw_inv, kw_opt,   kw_opt, },
		[op_boolean] = { kw_req,  kw_opt,   kw_opt,         kw_inv,     kw_inv, kw_inv, kw_opt,   kw_opt, },
		[op_combo]   = { kw_req,  kw_opt,   kw_opt,         kw_req,     kw_inv, kw_inv, kw_opt,   kw_opt, },
		[op_integer] = { kw_req,  kw_req,   kw_opt,         kw_inv,     kw_opt, kw_opt, kw_opt,   kw_opt, },
		[op_array]   = { kw_req,  kw_opt,   kw_opt,         kw_opt,     kw_inv, kw_inv, kw_opt,   kw_opt, },
		[op_feature] = { kw_req,  kw_opt,   kw_opt,         kw_inv,     kw_inv, kw_inv, kw_opt,   kw_opt, },
	};

	uint32_t i;
	for (i = 0; i < kwargs_count; ++i) {
		switch (keyword_validity[type][i]) {
		case kw_opt: break;
		case kw_inv:
			if (akw[i].set) {
				vm_error_at(wk, akw[i].node, "invalid keyword for option type");
				return false;
			}
			break;
		case kw_req:
			if (!akw[i].set) {
				vm_error(wk, "missing keyword '%s' for option type", akw[i].key);
				return false;
			}
			break;
		default: assert(false && "unreachable");
		}
	}

	obj val = 0;
	if (akw[kw_value].set) {
		val = akw[kw_value].val;
	} else {
		switch (type) {
		case op_string: val = make_str(wk, ""); break;
		case op_boolean: val = make_obj_bool(wk, true); break;
		case op_combo:
			if (!get_obj_array(wk, akw[kw_choices].val)->len) {
				vm_error_at(wk, akw[kw_choices].node, "combo option with no choices");
				return false;
			}

			val = obj_array_index(wk, akw[kw_choices].val, 0);
			break;
		case op_array:
		case op_shell_array:
			if (akw[kw_choices].set) {
				val = akw[kw_choices].val;
			} else {
				val = make_obj(wk, obj_array);
			}
			break;
		case op_feature:
			val = make_obj(wk, obj_feature_opt);
			set_obj_feature_opt(wk, val, feature_opt_auto);
			break;
		default: UNREACHABLE_RETURN;
		}
	}

	obj opt;
	opt = make_obj(wk, obj_option);
	struct obj_option *o = get_obj_option(wk, opt);
	o->name = an[0].val;
	o->type = type;
	o->min = akw[kw_min].val;
	o->max = akw[kw_max].val;
	o->choices = akw[kw_choices].val;
	o->yield = akw[kw_yield].set && get_obj_bool(wk, akw[kw_yield].val);
	o->description = akw[kw_description].val;
	o->deprecated = akw[kw_deprecated].val;
	o->ip = wk->vm.ip - 1;

	if (akw[kw_kind].set) {
		if (str_eql(&STR("default"), get_str(wk, akw[kw_kind].val))) {
			o->kind = build_option_kind_default;
		} else if (str_eql(&STR("prefixed_dir"), get_str(wk, akw[kw_kind].val))) {
			o->kind = build_option_kind_prefixed_dir;
		} else {
			vm_error_at(wk, akw[kw_kind].node, "invalid kind: %o", akw[kw_kind].val);
			return false;
		}
	}

	obj opts;
	if (wk->projects.len) {
		if (!validate_option_name(wk, an[0].node, an[0].val)) {
			return false;
		}

		opts = current_project(wk)->opts;
	} else {
		opts = wk->global_opts;
	}

	if (!create_option(wk, opts, opt, val)) {
		return false;
	}

	return true;
}

bool
func_get_option(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj opt;
	if (!get_option(wk, current_project(wk), get_str(wk, an[0].val), &opt)) {
		vm_error_at(wk, an[0].node, "undefined option");
		return false;
	}

	struct obj_option *o = get_obj_option(wk, opt);

	if (wk->vm.in_analyzer) {
		type_tag t = 0;
		switch (o->type) {
		case op_string: t =  tc_string; break;
		case op_boolean: t =  tc_bool; break;
		case op_combo: t = COMPLEX_TYPE(o->choices, complex_type_enum); break;
		case op_integer: t = tc_number; break;
		case op_shell_array: t = tc_array; break;
		case op_array: t = tc_array; break;
		case op_feature: t = tc_feature_opt; break;
		default: UNREACHABLE;
		}

		*res = make_typeinfo(wk, t);
	} else {
		*res = o->val;
	}
	return true;
}
