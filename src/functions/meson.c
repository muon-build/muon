#include "posix.h"

#include "coerce.h"
#include "compilers.h"
#include "functions/build_target.h"
#include "functions/common.h"
#include "functions/meson.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/path.h"
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
	if (!s_to_compiler_language(get_cstr(wk, an[0].val), &l)
	    || !obj_dict_geti(wk, current_project(wk)->compilers, l, obj)) {
		interp_error(wk, an[0].node, "no compiler found for '%s'", get_cstr(wk, an[0].val));
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

	*obj = current_project(wk)->cfg.name;
	return true;
}

static bool
func_meson_project_license(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = current_project(wk)->cfg.license;
	return true;
}

static bool
func_meson_project_version(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = current_project(wk)->cfg.version;
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

	*obj = current_project(wk)->cwd;
	return true;
}

static bool
func_meson_current_build_dir(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = current_project(wk)->build_dir;
	return true;
}

static bool
func_meson_project_source_root(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = current_project(wk)->source_root;
	return true;
}

static bool
func_meson_project_build_root(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = current_project(wk)->build_root;
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

static bool
func_meson_backend(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk, "ninja");
	return true;
}

static bool
func_meson_is_cross_build(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = false;
	return true;
}

static bool
func_meson_is_unity(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = false;
	return true;
}

static bool
func_meson_override_dependency(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_dependency }, ARG_TYPE_NULL };
	enum kwargs {
		kw_static, // ignored
		kw_native, // ignored
	};
	struct args_kw akw[] = {
		[kw_static] = { "static", obj_bool },
		[kw_native] = { "native", obj_bool },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	obj_dict_set(wk, wk->dep_overrides, an[0].val, an[1].val);
	return true;
}

struct process_script_commandline_ctx {
	uint32_t node;
	obj arr;
	uint32_t i;
};

static enum iteration_result
process_script_commandline_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct process_script_commandline_ctx *ctx = _ctx;

	struct obj *o = get_obj(wk, val);
	obj str;

	switch (o->type) {
	case obj_string:
		if (ctx->i) {
			str = val;
		} else {
			const char *p = get_cstr(wk, val);

			if (path_is_absolute(p)) {
				str = val;
			} else {
				char path[PATH_MAX];
				if (!path_join(path, PATH_MAX, get_cstr(wk, current_project(wk)->cwd), p)) {
					return false;
				}

				str = make_str(wk, path);
			}
		}
		break;
	case obj_custom_target:
		if (!obj_array_foreach(wk, o->dat.custom_target.output, ctx, process_script_commandline_iter)) {
			return false;
		}
		goto cont;
	case obj_build_target:
	case obj_external_program:
	case obj_file:
		if (!coerce_executable(wk, ctx->node, val, &str)) {
			return ir_err;
		}
		break;
	default:
		interp_error(wk, ctx->node, "invalid type for script commandline '%s'",
			obj_type_to_s(o->type));
		return ir_err;
	}

	obj_array_push(wk, ctx->arr, str);
cont:
	++ctx->i;
	return ir_cont;
}

static bool
func_meson_add_install_script(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB }, ARG_TYPE_NULL };
	enum kwargs {
		kw_install_tag, // ignored
		kw_skip_if_destdir, // ignored
	};
	struct args_kw akw[] = {
		[kw_install_tag] = { "install_tag", obj_string },
		[kw_skip_if_destdir] = { "skip_if_destdir", obj_bool },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	struct process_script_commandline_ctx ctx = {
		.node = an[0].node,
	};
	make_obj(wk, &ctx.arr, obj_array);

	if (!obj_array_foreach_flat(wk, an[0].val, &ctx, process_script_commandline_iter)) {
		return false;
	}

	obj_array_push(wk, wk->install_scripts, ctx.arr);
	return true;
}

const struct func_impl_name impl_tbl_meson[] = {
	{ "add_install_script", func_meson_add_install_script },
	{ "backend", func_meson_backend },
	{ "build_root", func_meson_global_build_root },
	{ "current_build_dir", func_meson_current_build_dir },
	{ "current_source_dir", func_meson_current_source_dir },
	{ "get_compiler", func_meson_get_compiler },
	{ "global_build_root", func_meson_global_build_root },
	{ "global_source_root", func_meson_global_source_root },
	{ "is_cross_build", func_meson_is_cross_build },
	{ "is_subproject", func_meson_is_subproject },
	{ "is_unity", func_meson_is_unity },
	{ "override_dependency", func_meson_override_dependency },
	{ "project_build_root", func_meson_project_build_root },
	{ "project_license", func_meson_project_license },
	{ "project_name", func_meson_project_name },
	{ "project_source_root", func_meson_project_source_root },
	{ "project_version", func_meson_project_version },
	{ "source_root", func_meson_global_source_root },
	{ "version", func_meson_version },
	{ NULL, NULL },
};
