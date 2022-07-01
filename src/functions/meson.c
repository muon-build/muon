#include "posix.h"

#include "coerce.h"
#include "compilers.h"
#include "error.h"
#include "functions/build_target.h"
#include "functions/common.h"
#include "functions/meson.h"
#include "lang/interpreter.h"
#include "log.h"
#include "options.h"
#include "platform/path.h"
#include "version.h"

static bool
func_meson_get_compiler(struct workspace *wk, obj _, uint32_t args_node, obj *res)
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
	    || !obj_dict_geti(wk, current_project(wk)->compilers, l, res)) {
		interp_error(wk, an[0].node, "no compiler found for '%s'", get_cstr(wk, an[0].val));
		return false;
	}

	return true;
}

static bool
func_meson_project_name(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->cfg.name;
	return true;
}

static bool
func_meson_project_license(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->cfg.license;
	return true;
}

static bool
func_meson_project_version(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->cfg.version;
	return true;
}

static bool
func_meson_version(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk, muon_version.meson_compat);
	return true;
}

static bool
func_meson_current_source_dir(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->cwd;
	return true;
}

static bool
func_meson_current_build_dir(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->build_dir;
	return true;
}

static bool
func_meson_project_source_root(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->source_root;
	return true;
}

static bool
func_meson_project_build_root(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->build_root;
	return true;
}

static bool
func_meson_global_source_root(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk, wk->source_root);
	return true;
}

static bool
func_meson_global_build_root(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk, wk->build_root);
	return true;
}

static bool
func_meson_is_subproject(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, wk->cur_project != 0);
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

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, false);
	return true;
}

static bool
func_meson_is_unity(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, false);
	return true;
}

static bool
func_meson_override_dependency(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_dependency }, ARG_TYPE_NULL };
	enum kwargs {
		kw_static,
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

	obj override_dict;

	if (akw[kw_static].set) {
		if (get_obj_bool(wk, akw[kw_static].val)) {
			override_dict = wk->dep_overrides_static;
		} else {
			override_dict = wk->dep_overrides_dynamic;
		}
	} else {
		switch (get_option_default_library(wk)) {
		case tgt_static_library:
			override_dict = wk->dep_overrides_static;
			break;
		default:
			override_dict = wk->dep_overrides_dynamic;
			break;
		}
	}

	obj_dict_set(wk, override_dict, an[0].val, an[1].val);
	return true;
}

static bool
func_meson_override_find_program(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = {
		{ obj_string },
		{ tc_file | tc_external_program | tc_build_target | tc_custom_target },
		ARG_TYPE_NULL
	};

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	obj override;

	switch (get_obj_type(wk, an[1].val)) {
	case obj_build_target:
	case obj_custom_target:
	case obj_file:
		make_obj(wk, &override, obj_array);
		obj_array_push(wk, override, an[1].val);

		obj ver = 0;
		if (!current_project(wk)->cfg.no_version) {
			ver = current_project(wk)->cfg.version;
		}
		obj_array_push(wk, override, ver);
		break;
	case obj_external_program:
		override = an[1].val;
		break;
	default:
		UNREACHABLE;
	}

	obj_dict_set(wk, wk->find_program_overrides, an[0].val, override);
	return true;
}

struct process_script_commandline_ctx {
	uint32_t node;
	obj arr;
	uint32_t i;
	bool allow_not_built;
	bool make_deps_default;
};

static enum iteration_result
process_script_commandline_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct process_script_commandline_ctx *ctx = _ctx;
	obj str;
	enum obj_type t = get_obj_type(wk, val);

	switch (t) {
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
		if (!ctx->allow_not_built) {
			goto type_error;
		}

		struct obj_custom_target *o = get_obj_custom_target(wk, val);
		if (ctx->make_deps_default) {
			o->flags |= custom_target_build_by_default;
		}

		if (!obj_array_foreach(wk, o->output, ctx, process_script_commandline_iter)) {
			return false;
		}
		goto cont;
	case obj_build_target: {
		if (!ctx->allow_not_built) {
			goto type_error;
		}

		struct obj_build_target *o = get_obj_build_target(wk, val);

		if (ctx->make_deps_default) {
			o->flags |= build_tgt_flag_build_by_default;
		}
	}
	//fallthrough
	case obj_external_program:
	case obj_file:
		if (!coerce_executable(wk, ctx->node, val, &str)) {
			return ir_err;
		}
		break;
	default:
type_error:
		interp_error(wk, ctx->node, "invalid type for script commandline '%s'",
			obj_type_to_s(t));
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
	struct args_norm an[] = { { ARG_TYPE_GLOB | tc_exe }, ARG_TYPE_NULL };
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
		.allow_not_built = true,
		.make_deps_default = true,
	};
	make_obj(wk, &ctx.arr, obj_array);

	if (!obj_array_foreach_flat(wk, an[0].val, &ctx, process_script_commandline_iter)) {
		return false;
	}

	obj_array_push(wk, wk->install_scripts, ctx.arr);
	return true;
}

static bool
func_meson_add_postconf_script(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB | tc_exe }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	struct process_script_commandline_ctx ctx = {
		.node = an[0].node,
	};
	make_obj(wk, &ctx.arr, obj_array);

	if (!obj_array_foreach_flat(wk, an[0].val, &ctx, process_script_commandline_iter)) {
		return false;
	}

	obj_array_push(wk, wk->postconf_scripts, ctx.arr);
	return true;
}

static bool
func_meson_add_dist_script(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB | tc_exe }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	struct process_script_commandline_ctx ctx = {
		.node = an[0].node,
		.allow_not_built = true,
	};
	make_obj(wk, &ctx.arr, obj_array);

	if (!obj_array_foreach_flat(wk, an[0].val, &ctx, process_script_commandline_iter)) {
		return false;
	}

	// TODO: uncomment when muon dist is implemented
	/* obj_array_push(wk, wk->dist_scripts, ctx.arr); */
	return true;
}

static bool
func_meson_get_cross_property(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { tc_any }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, ao, NULL)) {
		return false;
	}

	if (ao[0].set) {
		*res = ao[0].val;
	} else {
		interp_error(wk, an[0].node, "TODO: get cross property");
		return false;
	}

	return true;
}

static bool
func_meson_get_external_property(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { tc_any }, ARG_TYPE_NULL };
	enum kwargs { kw_native, };
	struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
		0
	};

	if (!interp_args(wk, args_node, an, ao, akw)) {
		return false;
	}

	if (ao[0].set) {
		*res = ao[0].val;
	} else {
		interp_error(wk, an[0].node, "TODO: get external property");
		return false;
	}

	return true;
}

static bool
func_meson_can_run_host_binaries(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, true); // TODO: can return false in cross compile
	return true;
}

static bool
func_meson_add_devenv(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_any }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	return true;
}

const struct func_impl_name impl_tbl_meson[] = {
	{ "add_devenv", func_meson_add_devenv },
	{ "add_dist_script", func_meson_add_dist_script },
	{ "add_install_script", func_meson_add_install_script },
	{ "add_postconf_script", func_meson_add_postconf_script },
	{ "backend", func_meson_backend, tc_string },
	{ "build_root", func_meson_global_build_root, tc_string },
	{ "can_run_host_binaries", func_meson_can_run_host_binaries, tc_bool },
	{ "current_build_dir", func_meson_current_build_dir, tc_string },
	{ "current_source_dir", func_meson_current_source_dir, tc_string },
	{ "get_compiler", func_meson_get_compiler, tc_compiler },
	{ "get_cross_property", func_meson_get_cross_property, tc_any },
	{ "get_external_property", func_meson_get_external_property, tc_any },
	{ "global_build_root", func_meson_global_build_root, tc_string },
	{ "global_source_root", func_meson_global_source_root, tc_string },
	{ "has_exe_wrapper", func_meson_can_run_host_binaries, tc_bool },
	{ "is_cross_build", func_meson_is_cross_build, tc_bool },
	{ "is_subproject", func_meson_is_subproject, tc_bool },
	{ "is_unity", func_meson_is_unity, tc_bool },
	{ "override_dependency", func_meson_override_dependency },
	{ "override_find_program", func_meson_override_find_program },
	{ "project_build_root", func_meson_project_build_root, tc_string },
	{ "project_license", func_meson_project_license, tc_string },
	{ "project_name", func_meson_project_name, tc_string },
	{ "project_source_root", func_meson_project_source_root, tc_string },
	{ "project_version", func_meson_project_version, tc_string },
	{ "source_root", func_meson_global_source_root, tc_string },
	{ "version", func_meson_version, tc_string },
	{ NULL, NULL },
};
