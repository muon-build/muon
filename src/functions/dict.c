#include "posix.h"

#include "functions/common.h"
#include "functions/dict.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_dict_has_key(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_bool)->dat.boolean = obj_dict_in(wk, rcvr, an[0].val);
	return true;
}

static bool
func_dict_get(struct workspace *wk, uint32_t rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { obj_any }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, ao, NULL)) {
		return false;
	}

	if (!obj_dict_index(wk, rcvr, an[0].val, res)) {
		if (ao[0].set) {
			*res = ao[0].val;
		} else {
			interp_error(wk, an[0].node, "key not in dictionary: '%s'", wk_objstr(wk, an[0].val));
			return false;
		}
	}
	return true;
}

const struct func_impl_name impl_tbl_dict[] = {
	{ "has_key", func_dict_has_key },
	{ "get", func_dict_get },
	{ NULL, NULL },
};
