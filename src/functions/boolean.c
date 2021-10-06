#include "posix.h"

#include "functions/common.h"
#include "functions/boolean.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_boolean_to_string(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *s = get_obj(wk, rcvr)->dat.boolean ? "true" : "false";
	*res = make_str(wk, s);

	return true;
}

static bool
func_boolean_to_int(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	int32_t val = get_obj(wk, rcvr)->dat.boolean ? 1 : 0;
	make_obj(wk, res, obj_number)->dat.num = val;

	return true;
}

const struct func_impl_name impl_tbl_boolean[] = {
	{ "to_int", func_boolean_to_int },
	{ "to_string", func_boolean_to_string },
	{ NULL, NULL },
};
