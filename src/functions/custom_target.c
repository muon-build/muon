#include "posix.h"

#include "functions/common.h"
#include "functions/custom_target.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_custom_target_to_list(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_custom_target(wk, rcvr)->output;
	return true;
}

static bool
func_custom_target_full_path(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	obj elem;
	if (!obj_array_flatten_one(wk, get_obj_custom_target(wk, rcvr)->output, &elem)) {
		interp_error(wk, args_node, "this custom_target has multiple outputs");
		return false;
	}

	*res = *get_obj_file(wk, elem);
	return true;
}

const struct func_impl_name impl_tbl_custom_target[] = {
	{ "full_path", func_custom_target_full_path },
	{ "to_list", func_custom_target_to_list },
	{ NULL, NULL },
};
