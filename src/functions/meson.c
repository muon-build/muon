/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Eli Schwartz <eschwartz@archlinux.org>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "args.h"
#include "backend/common_args.h"
#include "backend/output.h"
#include "buf_size.h"
#include "coerce.h"
#include "compilers.h"
#include "error.h"
#include "functions/kernel.h"
#include "functions/kernel/dependency.h"
#include "functions/meson.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "version.h"

static bool
func_meson_get_compiler(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_native,
	};
	struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	enum compiler_language l;
	if (!s_to_compiler_language(get_cstr(wk, an[0].val), &l)
		|| !obj_dict_geti(
			wk, current_project(wk)->toolchains[coerce_machine_kind(wk, &akw[kw_native])], l, res)) {
		vm_error_at(wk, an[0].node, "no compiler found for '%s'", get_cstr(wk, an[0].val));
		return false;
	}

	return true;
}

static bool
func_meson_project_name(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->cfg.name;
	return true;
}

static bool
func_meson_project_license(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->cfg.license;
	if (!*res) {
		*res = make_obj(wk, obj_array);
	}
	return true;
}

static bool
func_meson_project_license_files(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->cfg.license_files;
	if (!*res) {
		*res = make_obj(wk, obj_array);
	}
	return true;
}

static bool
func_meson_project_version(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->cfg.version;
	return true;
}

static bool
func_meson_version(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk, muon_version.meson_compat);
	return true;
}

static bool
func_meson_current_source_dir(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->cwd;
	return true;
}

static bool
func_meson_current_build_dir(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->build_dir;
	return true;
}

static bool
func_meson_project_source_root(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->source_root;
	return true;
}

static bool
func_meson_project_build_root(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = current_project(wk)->build_root;
	return true;
}

static bool
func_meson_global_source_root(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk, wk->source_root);
	return true;
}

static bool
func_meson_global_build_root(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk, wk->build_root);
	return true;
}

static bool
func_meson_build_options(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	obj options = ca_regenerate_build_command(wk, true);

	// remove the build directory from options
	obj_array_pop(wk, options);

	*res = join_args_shell(wk, options);
	return true;
}

static bool
func_meson_is_subproject(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, wk->cur_project != 0);
	return true;
}

static bool
func_meson_backend(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	switch (get_option_backend(wk)) {
	case backend_ninja: *res = make_str(wk, "ninja"); break;
	case backend_xcode: *res = make_str(wk, "xcode"); break;
	}

	return true;
}

static bool
is_cross_build(void)
{
	return !machine_definitions_eql(&build_machine, &host_machine);
}

static bool
func_meson_is_cross_build(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, is_cross_build());
	return true;
}

static bool
func_meson_is_unity(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, false);
	return true;
}

static bool
func_meson_override_dependency(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_dependency }, ARG_TYPE_NULL };
	enum kwargs {
		kw_static,
		kw_native,
	};
	struct args_kw akw[] = {
		[kw_static] = { "static", obj_bool },
		[kw_native] = { "native", obj_bool },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	enum machine_kind machine = coerce_machine_kind(wk, &akw[kw_native]);

	obj override_dict;

	if (akw[kw_static].set) {
		if (get_obj_bool(wk, akw[kw_static].val)) {
			override_dict = wk->dep_overrides_static[machine];
		} else {
			override_dict = wk->dep_overrides_dynamic[machine];
		}
	} else {
		switch (get_option_default_library(wk)) {
		case tgt_static_library: override_dict = wk->dep_overrides_static[machine]; break;
		default: override_dict = wk->dep_overrides_dynamic[machine]; break;
		}
	}

	obj d = make_obj(wk, obj_dependency);
	{
		// Clone this dependency and set its name to the name of the override
		struct obj_dependency *dep = get_obj_dependency(wk, d);
		*dep = *get_obj_dependency(wk, an[1].val);
		dep->name = an[0].val;
	}

	obj_dict_set(wk, override_dict, an[0].val, d);
	return true;
}

static bool
func_meson_override_find_program(struct workspace *wk, obj _, obj *res)
{
	type_tag tc_allowed = tc_file | tc_external_program | tc_build_target | tc_custom_target
			      | tc_python_installation;
	struct args_norm an[] = { { obj_string }, { tc_allowed }, ARG_TYPE_NULL };

	// TODO: why does override_find_program not accept a native keyword?
	enum machine_kind machine = coerce_machine_kind(wk, 0);

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj override;

	switch (get_obj_type(wk, an[1].val)) {
	case obj_build_target:
	case obj_custom_target:
	case obj_file:
		override = make_obj(wk, obj_array);
		obj_array_push(wk, override, an[1].val);

		obj ver = 0;
		if (!current_project(wk)->cfg.no_version) {
			ver = current_project(wk)->cfg.version;
		}
		obj_array_push(wk, override, ver);
		break;
	case obj_external_program:
	case obj_python_installation: override = an[1].val; break;
	default: UNREACHABLE;
	}

	obj_dict_set(wk, wk->find_program_overrides[machine], an[0].val, override);
	return true;
}

struct process_script_commandline_ctx {
	uint32_t node;
	obj arr;
	bool allow_not_built;
	bool make_deps_default;
};

static bool
process_script_commandline(struct workspace *wk, struct process_script_commandline_ctx *ctx, obj val)
{
	enum obj_type t = get_obj_type(wk, val);

	switch (t) {
	case obj_string: {
		if (get_obj_array(wk, ctx->arr)->len) {
			obj_array_push(wk, ctx->arr, val);
		} else {
			obj found_prog;
			struct find_program_ctx find_program_ctx = {
				.node = ctx->node,
				.res = &found_prog,
				.requirement = requirement_required,
				.machine = machine_kind_build,
			};

			if (!find_program(wk, &find_program_ctx, val)) {
				return false;
			}

			obj_array_extend(wk, ctx->arr, get_obj_external_program(wk, found_prog)->cmd_array);
		}
		break;
	}
	case obj_custom_target: {
		if (!ctx->allow_not_built) {
			goto type_error;
		}

		struct obj_custom_target *o = get_obj_custom_target(wk, val);
		if (ctx->make_deps_default) {
			o->flags |= custom_target_build_by_default;
		}

		obj val;
		obj_array_for(wk, o->output, val) {
			obj_array_push(wk, ctx->arr, *get_obj_file(wk, val));
		}
		break;
	}
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
	case obj_python_installation:
	case obj_file: {
		obj str, args;
		if (!coerce_executable(wk, ctx->node, val, &str, &args)) {
			return false;
		}

		obj_array_push(wk, ctx->arr, str);
		if (args) {
			obj_array_extend_nodup(wk, ctx->arr, args);
		}
		break;
	}
	default:
type_error:
		vm_error_at(wk, ctx->node, "invalid type for script commandline '%s'", obj_type_to_s(t));
		return false;
	}

	return true;
}

static bool
func_meson_add_install_script(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | tc_exe }, ARG_TYPE_NULL };
	enum kwargs {
		kw_install_tag, // ignored
		kw_skip_if_destdir,
		kw_dry_run,
	};
	struct args_kw akw[] = {
		[kw_install_tag] = { "install_tag", obj_string },
		[kw_skip_if_destdir] = { "skip_if_destdir", obj_bool },
		[kw_dry_run] = { "dry_run", obj_bool },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	struct process_script_commandline_ctx ctx = {
		.node = an[0].node,
		.allow_not_built = true,
		.make_deps_default = true,
	};
	ctx.arr = make_obj(wk, obj_array);

	obj v;
	obj_array_flat_for_(wk, an[0].val, v, iter) {
		if (!process_script_commandline(wk, &ctx, v)) {
			obj_array_flat_iter_end(wk, &iter);
			return false;
		}
	}

	if (!akw[kw_skip_if_destdir].set) {
		akw[kw_skip_if_destdir].val = make_obj_bool(wk, false);
	}

	if (!akw[kw_dry_run].set) {
		akw[kw_dry_run].val = make_obj_bool(wk, false);
	}

	obj install_script;
	install_script = make_obj(wk, obj_array);
	obj_array_push(wk, install_script, akw[kw_skip_if_destdir].val);
	obj_array_push(wk, install_script, akw[kw_dry_run].val);
	obj_array_push(wk, install_script, ctx.arr);
	obj_array_push(wk, wk->install_scripts, install_script);
	return true;
}

static bool
func_meson_add_postconf_script(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | tc_exe }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	struct process_script_commandline_ctx ctx = {
		.node = an[0].node,
	};
	ctx.arr = make_obj(wk, obj_array);

	obj v;
	obj_array_flat_for_(wk, an[0].val, v, iter) {
		if (!process_script_commandline(wk, &ctx, v)) {
			obj_array_flat_iter_end(wk, &iter);
			return false;
		}
	}

	obj_array_push(wk, wk->postconf_scripts, ctx.arr);
	return true;
}

static bool
func_meson_add_dist_script(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | tc_exe }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	struct process_script_commandline_ctx ctx = {
		.node = an[0].node,
		.allow_not_built = true,
	};
	ctx.arr = make_obj(wk, obj_array);

	obj v;
	obj_array_flat_for_(wk, an[0].val, v, iter) {
		if (!process_script_commandline(wk, &ctx, v)) {
			obj_array_flat_iter_end(wk, &iter);
			return false;
		}
	}

	// TODO: uncomment when muon dist is implemented
	/* obj_array_push(wk, wk->dist_scripts, ctx.arr); */
	return true;
}

static bool
meson_get_property(struct workspace *wk, obj dict, obj key, obj fallback, obj *res)
{
	if (obj_dict_index(wk, dict, key, res)) {
		return true;
	}

	if (fallback) {
		*res = fallback;
		return true;
	}

	vm_error(wk,"unknown property %o", key);
	return false;
}

static bool
func_meson_get_cross_property(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { obj_string }, { tc_any, .optional = true }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	return meson_get_property(wk, wk->machine_properties[machine_kind_host], an[0].val, an[1].val, res);
}

static bool
func_meson_get_external_property(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { obj_string }, { tc_any, .optional = true }, ARG_TYPE_NULL };
	enum kwargs {
		kw_native,
	};
	struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	return meson_get_property(
		wk, wk->machine_properties[coerce_machine_kind(wk, &akw[kw_native])], an[0].val, an[1].val, res);
}

static bool
func_meson_can_run_host_binaries(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	// TODO: This could actually still be true even when cross compiling if an
	// exe wrapper is defined.  But muon doesn't support that yet.
	*res = make_obj_bool(wk, !is_cross_build());
	return true;
}

static bool
func_meson_add_devenv(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { tc_any }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	return true;
}

const struct func_impl impl_tbl_meson[] = {
	{ "add_devenv", func_meson_add_devenv },
	{ "add_dist_script", func_meson_add_dist_script },
	{ "add_install_script", func_meson_add_install_script },
	{ "add_postconf_script", func_meson_add_postconf_script },
	{ "backend", func_meson_backend, tc_string },
	{ "build_options", func_meson_build_options, tc_string },
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
	{ "project_license_files", func_meson_project_license_files, tc_string },
	{ "project_name", func_meson_project_name, tc_string },
	{ "project_source_root", func_meson_project_source_root, tc_string },
	{ "project_version", func_meson_project_version, tc_string },
	{ "source_root", func_meson_global_source_root, tc_string },
	{ "version", func_meson_version, tc_string, true },
	{ NULL, NULL },
};

static obj
compiler_dict_to_str_dict(struct workspace *wk, obj d[machine_kind_count])
{
	obj res;
	res = make_obj(wk, obj_dict);

	for (enum machine_kind machine = 0; machine < machine_kind_count; ++machine) {
		obj r;
		r = make_obj(wk, obj_dict);

		obj k, v;
		obj_dict_for(wk, d[machine], k, v) {
			obj_dict_set(wk, r, make_str(wk, compiler_language_to_s(k)), v);
		}

		obj_dict_set(wk, res, make_str(wk, machine_kind_to_s(machine)), r);
	}

	return res;
}

static bool
func_meson_project(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct project *proj = current_project(wk);

	*res = make_obj(wk, obj_dict);

	if (!proj) {
		return true;
	}

	obj_dict_set(wk, *res, make_str(wk, "opts"), proj->opts);
	obj_dict_set(wk, *res, make_str(wk, "toolchains"), compiler_dict_to_str_dict(wk, proj->toolchains));
	obj_dict_set(wk, *res, make_str(wk, "args"), compiler_dict_to_str_dict(wk, proj->args));
	obj_dict_set(wk, *res, make_str(wk, "link_args"), compiler_dict_to_str_dict(wk, proj->link_args));
	return true;
}

static bool
func_meson_register_dependency_handler(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = {
		{ tc_string },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_pkgconfig,
		kw_builtin,
		kw_system,
		kw_config_tool,
		kw_order,
	};
	struct args_kw akw[] = {
		[kw_pkgconfig] = { "pkgconfig", tc_capture },
		[kw_builtin] = { "builtin", tc_capture },
		[kw_system] = { "system", tc_capture },
		[kw_config_tool] = { "config_tool", tc_capture },
		[kw_order] = { "order", TYPE_TAG_LISTIFY | tc_string },
		0,
	};
	const struct {
		enum kwargs kw;
		enum dependency_lookup_method method;
	} kwarg_to_method[] = {
		{ kw_pkgconfig, dependency_lookup_method_pkgconfig },
		{ kw_builtin, dependency_lookup_method_builtin },
		{ kw_system, dependency_lookup_method_system },
		{ kw_config_tool, dependency_lookup_method_config_tool },
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	obj handler_dict;
	handler_dict = make_obj(wk, obj_dict);

	bool set_any = false;

	if (akw[kw_order].set) {
		obj method;
		obj_array_for(wk, akw[kw_order].val, method) {
			enum dependency_lookup_method m;
			if (!dependency_lookup_method_from_s(get_str(wk, method), &m)) {
				vm_error_at(wk, akw[kw_order].node, "invalid dependency method %o", method);
				return false;
			}

			uint32_t i;
			enum kwargs kw = 0;
			for (i = 0; i < ARRAY_LEN(kwarg_to_method); ++i) {
				if (m == kwarg_to_method[i].method) {
					kw = kwarg_to_method[i].kw;
					break;
				}
			}

			obj v = akw[i].val;
			if (i == ARRAY_LEN(kwarg_to_method) || !akw[kw].set) {
				v = obj_bool_true;
			}

			obj_dict_seti(wk, handler_dict, m, v);
			set_any = true;
		}
	} else {
		uint32_t i;
		for (i = 0; i < ARRAY_LEN(kwarg_to_method); ++i) {
			enum kwargs kw = kwarg_to_method[i].kw;
			if (!akw[kw].set) {
				continue;
			}

			obj_dict_seti(wk, handler_dict, kwarg_to_method[i].method, akw[i].val);
			set_any = true;
		}
	}

	if (!set_any) {
		vm_error(wk, "No handlers defined.");
	}

	obj_dict_set(wk, wk->dependency_handlers, an[0].val, handler_dict);
	return true;
}

static bool
func_meson_argv0(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	*res = make_str(wk, wk->argv0);
	return true;
}

static bool
func_meson_private_dir(struct workspace *wk, obj _, obj *res)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	*res = make_str(wk, output_path.private_dir);
	return true;
}

static bool
func_meson_has_compiler(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_native,
	};
	struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	enum compiler_language l;
	if (!s_to_compiler_language(get_cstr(wk, an[0].val), &l)) {
		vm_error_at(wk, an[0].node, "unknown compiler language: '%s'", get_cstr(wk, an[0].val));
		return false;
	}

	*res = make_obj_bool(wk,
		obj_dict_geti(wk, current_project(wk)->toolchains[coerce_machine_kind(wk, &akw[kw_native])], l, &_));

	return true;
}

static bool
func_meson_set_external_properties(struct workspace *wk, obj _, obj *res)
{
	struct args_norm an[] = { { COMPLEX_TYPE_PRESET(tc_cx_dict_of_str) }, ARG_TYPE_NULL };
	enum kwargs {
		kw_native,
	};
	struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	obj *dest = &wk->machine_properties[coerce_machine_kind(wk, &akw[kw_native])];

	obj merged;
	obj_dict_merge(wk, *dest, an[0].val, &merged);
	*dest = merged;

	return true;
}

const struct func_impl impl_tbl_meson_internal[] = {
	{ "project", func_meson_project, tc_dict, .desc = "return a dict containing read-only properties of the current project"  },
	{ "register_dependency_handler", func_meson_register_dependency_handler, .desc = "register custom callbacks to run when a specific dependency lookup is invoked" },
	{ "argv0", func_meson_argv0, tc_string, .desc = "returns the argv[0] that was used to invoke muon itself" },
	{ "private_dir", func_meson_private_dir, tc_string, .desc = "returns the path to muon's private directory in the build folder" },
	{ "has_compiler", func_meson_has_compiler, tc_bool, .desc = "returns whether or not a compiler for the given language has been configured"  },
	{ "set_external_properties", func_meson_set_external_properties, .desc = "set properties to be accessed by meson.get_cross_property() and meson.get_external_property()"  },
	{ NULL, NULL },
};
