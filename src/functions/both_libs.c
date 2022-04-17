#include "posix.h"

#include "functions/build_target.h"
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

static bool
func_both_libs_name(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_build_target(wk, get_obj_both_libs(wk, rcvr)->dynamic_lib)->name;
	return true;
}

static bool
func_both_libs_private_dir_include(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_include_directory);
	struct obj_include_directory *inc = get_obj_include_directory(wk, *res);

	inc->path = get_obj_build_target(wk, get_obj_both_libs(wk, rcvr)->dynamic_lib)->private_path;
	return true;
}

static bool
func_both_libs_extract_objects(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB | tc_string | tc_file | tc_custom_target | tc_generated_list }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	return build_target_extract_objects(wk, get_obj_both_libs(wk, rcvr)->dynamic_lib, an[0].node, res, an[0].val);
}

const struct func_impl_name impl_tbl_both_libs[] = {
	{ "get_shared_lib", func_both_libs_get_shared_lib },
	{ "get_static_lib", func_both_libs_get_static_lib },
	{ "found", func_both_libs_found },
	{ "full_path", func_both_libs_full_path },
	{ "path", func_both_libs_full_path },
	{ "name", func_both_libs_name },
	{ "private_dir_include", func_both_libs_private_dir_include },
	{ "extract_objects", func_both_libs_extract_objects },
	{ NULL, NULL },
};
