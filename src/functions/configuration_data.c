/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "lang/func_lookup.h"
#include "functions/configuration_data.h"
#include "lang/typecheck.h"

FUNC_IMPL(configuration_data, set_quoted, 0, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_description, // TODO
	};
	struct args_kw akw[] = {
		[kw_description] = { "description", obj_string, },
		0
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	obj dict = get_obj_configuration_data(wk, self)->dict;

	const char *s = get_cstr(wk, an[1].val);
	obj str = make_str(wk, "\"");

	for (; *s; ++s) {
		if (*s == '"') {
			str_app(wk, &str, "\\");
		}

		str_appn(wk, &str, s, 1);
	}

	str_app(wk, &str, "\"");

	obj_dict_set(wk, dict, an[0].val, str);
	return true;
}

FUNC_IMPL(configuration_data, set, 0, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, { tc_string | tc_number | tc_bool }, ARG_TYPE_NULL };
	enum kwargs {
		kw_description, // ingnored
	};
	struct args_kw akw[] = {
		[kw_description] = { "description", obj_string, },
		0
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	obj dict = get_obj_configuration_data(wk, self)->dict;

	obj_dict_set(wk, dict, an[0].val, an[1].val);

	return true;
}

FUNC_IMPL(configuration_data, set10, 0, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, { obj_bool }, ARG_TYPE_NULL };
	enum kwargs {
		kw_description, // ignored
	};
	struct args_kw akw[] = {
		[kw_description] = { "description", obj_string, },
		0
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	obj dict = get_obj_configuration_data(wk, self)->dict;

	obj n;
	n = make_obj(wk, obj_number);
	set_obj_number(wk, n, get_obj_bool(wk, an[1].val) ? 1 : 0);
	obj_dict_set(wk, dict, an[0].val, n);

	return true;
}

static bool
configuration_data_get(struct workspace *wk, uint32_t err_node, obj conf, obj key, obj def, obj *res)
{
	obj dict = get_obj_configuration_data(wk, conf)->dict;

	if (!obj_dict_index(wk, dict, key, res)) {
		if (def) {
			*res = def;
		} else {
			vm_error_at(wk, err_node, "key '%s' not found", get_cstr(wk, key));
			return false;
		}
	}

	return true;
}

FUNC_IMPL(configuration_data, get, tc_any, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, { tc_any, .optional = true }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	return configuration_data_get(wk, an[0].node, self, an[0].val, an[1].val, res);
}

FUNC_IMPL(configuration_data, get_unquoted, tc_any, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, { tc_any, .optional = true }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj v;
	if (!configuration_data_get(wk, an[0].node, self, an[0].val, an[1].val, &v)) {
		return false;
	}

	const char *s = get_cstr(wk, v);
	uint32_t l = strlen(s);

	if (l >= 2 && s[0] == '"' && s[l - 1] == '"') {
		*res = make_strn(wk, &s[1], l - 2);
	} else {
		*res = v;
	}

	return true;
}

static enum iteration_result
obj_dict_keys_iter(struct workspace *wk, void *_ctx, obj k, obj _v)
{
	obj *res = _ctx;

	obj_array_push(wk, *res, k);

	return ir_cont;
}

FUNC_IMPL(configuration_data, keys, tc_array, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	obj dict = get_obj_configuration_data(wk, self)->dict;

	*res = make_obj(wk, obj_array);
	obj_dict_foreach(wk, dict, res, obj_dict_keys_iter);
	return true;
}

FUNC_IMPL(configuration_data, has, tc_bool, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj _, dict = get_obj_configuration_data(wk, self)->dict;
	*res = make_obj_bool(wk, obj_dict_index(wk, dict, an[0].val, &_));
	return true;
}

FUNC_IMPL(configuration_data, merge_from, 0, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_configuration_data }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj_dict_merge_nodup(
		wk, get_obj_configuration_data(wk, self)->dict, get_obj_configuration_data(wk, an[0].val)->dict);
	return true;
}

FUNC_REGISTER(configuration_data)
{
	FUNC_IMPL_REGISTER(configuration_data, get);
	FUNC_IMPL_REGISTER(configuration_data, get_unquoted);
	FUNC_IMPL_REGISTER(configuration_data, has);
	FUNC_IMPL_REGISTER(configuration_data, keys);
	FUNC_IMPL_REGISTER(configuration_data, merge_from);
	FUNC_IMPL_REGISTER(configuration_data, set);
	FUNC_IMPL_REGISTER(configuration_data, set10);
	FUNC_IMPL_REGISTER(configuration_data, set_quoted);
}
