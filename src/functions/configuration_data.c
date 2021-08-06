#include "posix.h"

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

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
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

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	uint32_t dict = get_obj(wk, rcvr)->dat.configuration_data.dict;

	if (!ensure_not_in(wk, an[0].node, dict, an[0].val)) {
		return false;
	}

	obj_dict_set(wk, dict, an[0].val, an[1].val);

	return true;
}

const struct func_impl_name impl_tbl_configuration_data[] = {
	{ "set", func_configuration_data_set },
	{ "set_quoted", func_configuration_data_set_quoted },
	{ NULL, NULL },
};
