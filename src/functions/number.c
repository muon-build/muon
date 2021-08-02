#include "posix.h"

#include "functions/common.h"
#include "functions/number.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_number_to_string(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	struct obj *str = make_obj(wk, obj, obj_string);
	str->dat.str = wk_str_pushf(wk, "%ld", (intmax_t)get_obj(wk, rcvr)->dat.num);

	return true;
}

const struct func_impl_name impl_tbl_number[] = {
	{ "to_string", func_number_to_string },
	{ NULL, NULL },
};
