#include "posix.h"

#include "functions/common.h"
#include "functions/configuration_data.h"
#include "interpreter.h"
#include "log.h"

static bool
func_configuration_data_set(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, { obj_any }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	uint32_t dict = get_obj(wk, rcvr)->dat.configuration_data.dict;

	bool res;
	if (!obj_dict_in(wk, an[0].val, dict, &res)) {
		return false;
	} else if (res) {
		interp_error(wk, an[0].node, "duplicate key in configuration_data");
		return false;
	}

	obj_dict_set(wk, dict, an[0].val, an[1].val);

	return true;
}

const struct func_impl_name impl_tbl_configuration_data[] = {
	{ "set", func_configuration_data_set },
	{ NULL, NULL },
};
