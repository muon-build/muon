#include "posix.h"

#include "functions/common.h"
#include "functions/dict.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_has_key(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	bool res;
	if (!obj_dict_in(wk, an[0].val, rcvr, &res)) {
		return false;
	}

	make_obj(wk, obj, obj_bool)->dat.boolean = res;
	return true;
}

const struct func_impl_name impl_tbl_dict[] = {
	{ "has_key", func_has_key },
	{ NULL, NULL },
};
