#include "posix.h"

#include "functions/common.h"
#include "functions/external_library.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_external_library_found(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, get_obj_external_library(wk, rcvr)->found);
	return true;
}

static bool
func_external_library_type_name(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk, "library");
	return true;
}

const struct func_impl_name impl_tbl_external_library[] = {
	{ "found", func_external_library_found, tc_bool },
	{ "type_name", func_external_library_type_name, tc_string },
	{ NULL, NULL },
};
