#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/configuration_data.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
ensure_not_in(struct workspace *wk, uint32_t node, obj dict, obj key)
{
	if (obj_dict_in(wk, dict, key)) {
		interp_error(wk, node, "duplicate key in configuration_data");
		return false;
	}

	return true;
}

static bool
func_configuration_data_set_quoted(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_description, // TODO
	};
	struct args_kw akw[] = {
		[kw_description] = { "description", obj_string, },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	obj dict = get_obj(wk, rcvr)->dat.configuration_data.dict;
	if (!ensure_not_in(wk, an[0].node, dict, an[0].val)) {
		return false;
	}

	obj val;
	const char *s = get_cstr(wk, an[1].val);
	str str = wk_str_push(wk, "\"");

	for (; *s; ++s) {
		if (*s == '"') {
			wk_str_app(wk, &str, "\\");
		}

		wk_str_appn(wk, &str, s, 1);
	}

	wk_str_app(wk, &str, "\"");

	make_obj(wk, &val, obj_string)->dat.str = str;
	obj_dict_set(wk, dict, an[0].val, val);

	return true;
}

static bool
func_configuration_data_set(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_any }, ARG_TYPE_NULL };
	enum kwargs {
		kw_description, // ingnored
	};
	struct args_kw akw[] = {
		[kw_description] = { "description", obj_string, },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	obj dict = get_obj(wk, rcvr)->dat.configuration_data.dict;

	if (!ensure_not_in(wk, an[0].node, dict, an[0].val)) {
		return false;
	}

	obj_dict_set(wk, dict, an[0].val, an[1].val);

	return true;
}

static bool
func_configuration_data_set10(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_bool }, ARG_TYPE_NULL };
	enum kwargs {
		kw_description, // ignored
	};
	struct args_kw akw[] = {
		[kw_description] = { "description", obj_string, },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	obj dict = get_obj(wk, rcvr)->dat.configuration_data.dict;

	if (!ensure_not_in(wk, an[0].node, dict, an[0].val)) {
		return false;
	}

	obj n;
	make_obj(wk, &n, obj_number)->dat.num = get_obj(wk, an[1].val)->dat.boolean ? 1 : 0;
	obj_dict_set(wk, dict, an[0].val, n);

	return true;
}

static bool
configuration_data_get(struct workspace *wk, uint32_t err_node, obj conf,
	obj key, obj def, obj *res)
{
	obj dict = get_obj(wk, conf)->dat.configuration_data.dict;

	if (!obj_dict_index(wk, dict, key, res)) {
		if (def) {
			*res = def;
		} else {
			interp_error(wk, err_node, "key '%s' not found", get_cstr(wk, key));
			return false;
		}
	}

	return true;
}

static bool
func_configuration_data_get(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { obj_any }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, ao, NULL)) {
		return false;
	}

	return configuration_data_get(wk, an[0].node, rcvr, an[0].val, ao[0].val, res);
}

static bool
func_configuration_data_get_unquoted(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { obj_any }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, ao, NULL)) {
		return false;
	}

	obj v;
	if (!configuration_data_get(wk, an[0].node, rcvr, an[0].val, ao[0].val, &v)) {
		return false;
	}

	const char *s = get_cstr(wk, v);
	uint32_t l = strlen(s);

	if (l >= 2 && s[0] == '"' && s[l - 1] == '"') {
		make_obj(wk, res, obj_string)->dat.str = wk_str_pushn(wk, &s[1], l - 2);
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

static bool
func_configuration_data_keys(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	obj dict = get_obj(wk, rcvr)->dat.configuration_data.dict;

	make_obj(wk, res, obj_array);
	obj_dict_foreach(wk, dict, res, obj_dict_keys_iter);
	return true;
}

const struct func_impl_name impl_tbl_configuration_data[] = {
	{ "set", func_configuration_data_set },
	{ "set10", func_configuration_data_set10 },
	{ "set_quoted", func_configuration_data_set_quoted },
	{ "get", func_configuration_data_get },
	{ "get_unquoted", func_configuration_data_get_unquoted },
	{ "keys", func_configuration_data_keys },
	{ NULL, NULL },
};
