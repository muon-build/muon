#include "posix.h"

#include "compilers.h"
#include "functions/common.h"
#include "functions/meson.h"
#include "lang/interpreter.h"
#include "log.h"
#include "version.h"

static bool
func_meson_get_compiler(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs { kw_native, };
	struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum compiler_language l;
	if (!s_to_compiler_language(wk_objstr(wk, an[0].val), &l)
	    || !obj_dict_geti(wk, current_project(wk)->compilers, l, obj)) {
		interp_error(wk, an[0].node, "no compiler found for '%s'", wk_objstr(wk, an[0].val));
		return false;
	}

	return true;
}

static bool
func_meson_project_name(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = current_project(wk)->cfg.name;
	return true;
}

static bool
func_meson_project_version(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = current_project(wk)->cfg.version;
	return true;
}

static bool
func_meson_version(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = make_str(wk, muon_version.meson_compat);
	return true;
}

static bool
func_meson_current_source_dir(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = current_project(wk)->cwd;
	return true;
}

static bool
func_meson_current_build_dir(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = current_project(wk)->build_dir;
	return true;
}

static bool
func_meson_global_source_root(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = make_str(wk, wk->source_root);
	return true;
}

static bool
func_meson_global_build_root(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = make_str(wk, wk->build_root);
	return true;
}

static bool
func_meson_is_subproject(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = wk->cur_project != 0;
	return true;
}

const struct func_impl_name impl_tbl_meson[] = {
	{ "get_compiler", func_meson_get_compiler },
	{ "project_name", func_meson_project_name },
	{ "project_version", func_meson_project_version },
	{ "version", func_meson_version },
	{ "current_source_dir", func_meson_current_source_dir },
	{ "current_build_dir", func_meson_current_build_dir },
	{ "source_root", func_meson_global_source_root },
	{ "global_source_root", func_meson_global_source_root },
	{ "build_root", func_meson_global_build_root },
	{ "global_build_root", func_meson_global_build_root },
	{ "is_subproject", func_meson_is_subproject },
	{ NULL, NULL },
};
