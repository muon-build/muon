#include "posix.h"

#include "functions/common.h"
#include "functions/array.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_array_length(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_number);
	set_obj_number(wk, *obj, get_obj_array(wk, rcvr)->len);
	return true;
}

static bool
func_array_get(struct workspace *wk, uint32_t rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_number }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { obj_any }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, ao, NULL)) {
		return false;
	}

	int64_t i = get_obj_number(wk, an[0].val);

	if (!bounds_adjust(wk, get_obj_array(wk, rcvr)->len, &i)) {
		if (ao[0].set) {
			*res = ao[0].val;
		} else {
			interp_error(wk, an[0].node, "index out of bounds");
			return false;
		}
	} else {
		obj_array_index(wk, rcvr, i, res);
	}

	return true;
}

static bool
func_array_contains(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_any }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, obj_array_in(wk, rcvr, an[0].val));
	return true;
}


const struct func_impl_name impl_tbl_array[] = {
	{ "length", func_array_length },
	{ "get", func_array_get },
	{ "contains", func_array_contains },
	{ NULL, NULL },
};
