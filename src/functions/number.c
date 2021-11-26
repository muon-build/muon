#include "posix.h"

#include <inttypes.h>

#include "functions/common.h"
#include "functions/number.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_number_is_odd(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = (get_obj(wk, rcvr)->dat.num & 1) != 0;
	return true;
}

static bool
func_number_is_even(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = (get_obj(wk, rcvr)->dat.num & 1) == 0;
	return true;
}

static bool
func_number_to_string(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = make_strf(wk, "%" PRId64, get_obj(wk, rcvr)->dat.num);
	return true;
}

const struct func_impl_name impl_tbl_number[] = {
	{ "to_string", func_number_to_string },
	{ "is_even", func_number_is_even },
	{ "is_odd", func_number_is_odd },
	{ NULL, NULL },
};
