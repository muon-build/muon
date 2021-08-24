#include "posix.h"

#include "functions/common.h"
#include "functions/array.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_length(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_number)->dat.num = get_obj(wk, rcvr)->dat.arr.len;
	return true;
}

const struct func_impl_name impl_tbl_array[] = {
	{ "length", func_length },
	{ NULL, NULL },
};
