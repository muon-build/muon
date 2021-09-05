#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/configuration_data.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
ensure_not_in(struct workspace *wk, uint32_t node, uint32_t dict, uint32_t key)
{
	if (obj_dict_in(wk, key, dict)) {
		interp_error(wk, node, "duplicate key in configuration_data");
		return false;
	}

	return true;
}

static bool
func_configuration_data_set_quoted(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
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

	uint32_t dict = get_obj(wk, rcvr)->dat.configuration_data.dict;
	if (!ensure_not_in(wk, an[0].node, dict, an[0].val)) {
		return false;
	}

	uint32_t val;
	make_obj(wk, &val, obj_string)->dat.str = wk_str_pushf(wk, "\"%s\"", wk_objstr(wk, an[1].val));

	obj_dict_set(wk, dict, an[0].val, val);

	return true;
}

static bool
func_configuration_data_set(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, { obj_any }, ARG_TYPE_NULL };
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

	uint32_t dict = get_obj(wk, rcvr)->dat.configuration_data.dict;

	if (!ensure_not_in(wk, an[0].node, dict, an[0].val)) {
		return false;
	}

	obj_dict_set(wk, dict, an[0].val, an[1].val);

	return true;
}

static bool
configuration_data_get(struct workspace *wk, uint32_t err_node, uint32_t conf,
	uint32_t key, uint32_t def, uint32_t *obj)
{
	uint32_t dict = get_obj(wk, conf)->dat.configuration_data.dict;

	if (!obj_dict_index(wk, dict, key, obj)) {
		if (def) {
			*obj = def;
		} else {
			interp_error(wk, err_node, "key '%s' not found", wk_objstr(wk, key));
			return false;
		}
	}

	return true;
}

static bool
func_configuration_data_get(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { obj_any }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, ao, NULL)) {
		return false;
	}

	return configuration_data_get(wk, an[0].node, rcvr, an[0].val, ao[0].val, obj);
}

static bool
func_configuration_data_get_unquoted(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { obj_any }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, ao, NULL)) {
		return false;
	}

	uint32_t res;
	if (!configuration_data_get(wk, an[0].node, rcvr, an[0].val, ao[0].val, &res)) {
		return false;
	}

	const char *s = wk_objstr(wk, res);
	uint32_t l = strlen(s);

	if (l >= 2 && s[0] == '"' && s[l - 1] == '"') {
		make_obj(wk, obj, obj_string)->dat.str = wk_str_pushn(wk, &s[1], l - 2);
	} else {
		*obj = res;
	}

	return true;
}

static enum iteration_result
obj_dict_keys_iter(struct workspace *wk, void *_ctx, uint32_t k, uint32_t _v)
{
	uint32_t *obj = _ctx;

	obj_array_push(wk, *obj, k);

	return ir_cont;
}

static bool
func_configuration_data_keys(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	uint32_t dict = get_obj(wk, rcvr)->dat.configuration_data.dict;

	make_obj(wk, obj, obj_array);
	obj_dict_foreach(wk, dict, obj, obj_dict_keys_iter);
	return true;
}

const struct func_impl_name impl_tbl_configuration_data[] = {
	{ "set", func_configuration_data_set },
	{ "set_quoted", func_configuration_data_set_quoted },
	{ "get", func_configuration_data_get },
	{ "get_unquoted", func_configuration_data_get_unquoted },
	{ "keys", func_configuration_data_keys },
	{ NULL, NULL },
};
