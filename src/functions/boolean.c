#include "posix.h"

#include "functions/common.h"
#include "functions/boolean.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_boolean_to_int(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	int32_t val;
	if (get_obj(wk, rcvr)->dat.boolean) {
		val = 1;
	} else {
		val = 0;
	}

	make_obj(wk, obj, obj_number)->dat.num = val;

	return true;
}

const struct func_impl_name impl_tbl_boolean[] = {
	{ "to_int", func_boolean_to_int },
	{ NULL, NULL },
};
