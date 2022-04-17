#include "posix.h"

#include "functions/both_libs.h"
#include "functions/common.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_both_libs_get_shared_lib(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_both_libs(wk, rcvr)->dynamic_lib;
	return true;
}

static bool
func_both_libs_get_static_lib(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_both_libs(wk, rcvr)->static_lib;
	return true;
}

static bool
func_both_libs_full_path(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_build_target(wk, get_obj_both_libs(wk, rcvr)->dynamic_lib)->build_path;
	return true;
}

static bool
func_both_libs_found(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, true);
	return true;
}

const struct func_impl_name impl_tbl_both_libs[] = {
	{ "get_shared_lib", func_both_libs_get_shared_lib },
	{ "get_static_lib", func_both_libs_get_static_lib },
	{ "found", func_both_libs_found },
	{ "full_path", func_both_libs_full_path },
	{ "path", func_both_libs_full_path },
	{ NULL, NULL },
};
