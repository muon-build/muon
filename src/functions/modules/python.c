/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "embedded.h"
#include "external/tinyjson.h"
#include "functions/external_program.h"
#include "functions/modules/python.h"
#include "install.h"
#include "lang/object.h"
#include "lang/typecheck.h"
#include "log.h"
#include "options.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

static bool
introspect_python_interpreter(struct workspace *wk, const char *path, struct obj_python_installation *python)
{
	struct source src;
	if (!embedded_get("python/python_info.py", &src)) {
		return false;
	}

	struct run_cmd_ctx cmd_ctx = { 0 };
	char *const var_args[] = { (char *)path, "-c", (char *)src.src, 0 };
	if (!run_cmd_argv_checked(&cmd_ctx, var_args, NULL, 0)) {
		return false;
	}

	obj res_introspect;
	{
		bool success = muon_json_to_obj(wk, &TSTR_STR(&cmd_ctx.out), &res_introspect);
		run_cmd_ctx_destroy(&cmd_ctx);

		if (!success) {
			return false;
		}
	}

	if (get_obj_type(wk, res_introspect) != obj_dict) {
		LOG_E("introspection object is not a dictionary");
		return false;
	}

	struct {
		const char *key;
		obj *dest;
	} key_to_dest[] = {
		{ "version", &python->language_version },
		{ "sysconfig_paths", &python->sysconfig_paths },
		{ "variables", &python->sysconfig_vars },
		{ "install_paths", &python->install_paths },
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(key_to_dest); ++i) {
		if (!obj_dict_index_str(wk, res_introspect, key_to_dest[i].key, key_to_dest[i].dest)) {
			LOG_E("introspection object missing key '%s'", key_to_dest[i].key);
			return false;
		}
	}

	return true;
}

static bool
python_module_present(struct workspace *wk, const char *pythonpath, const char *mod)
{
	struct run_cmd_ctx cmd_ctx = { 0 };

	TSTR(importstr);
	tstr_pushf(wk, &importstr, "import %s", mod);

	char *const *args = (char *const[]){ (char *)pythonpath, "-c", importstr.buf, 0 };

	bool present = run_cmd_argv(&cmd_ctx, args, NULL, 0) && cmd_ctx.status == 0;

	run_cmd_ctx_destroy(&cmd_ctx);

	return present;
}

struct iter_mod_ctx {
	const char *pythonpath;
	uint32_t node;
	enum requirement_type requirement;
};

static enum iteration_result
iterate_required_module_list(struct workspace *wk, void *ctx, obj val)
{
	struct iter_mod_ctx *_ctx = ctx;
	const char *mod = get_cstr(wk, val);

	if (python_module_present(wk, _ctx->pythonpath, mod)) {
		return ir_cont;
	}

	if (_ctx->requirement == requirement_required) {
		vm_error_at(wk, _ctx->node, "python: required module '%s' not found", mod);
	}

	return ir_err;
}

static bool
build_python_installation(struct workspace *wk, obj self, obj *res, struct tstr cmd_path, bool found, bool pure)
{
	*res = make_obj(wk, obj_python_installation);
	struct obj_python_installation *python = get_obj_python_installation(wk, *res);
	python->pure = pure;
	python->prog = make_obj(wk, obj_external_program);
	struct obj_external_program *ep = get_obj_external_program(wk, python->prog);
	ep->found = found;
	ep->cmd_array = make_obj(wk, obj_array);
	obj_array_push(wk, ep->cmd_array, tstr_into_str(wk, &cmd_path));

	if (found && !introspect_python_interpreter(wk, cmd_path.buf, python)) {
		vm_error(wk, "failed to introspect python");
		return false;
	}

	return true;
}

static bool
func_module_python_find_installation(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string, .optional = true }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_disabler,
		kw_modules,
		kw_pure,
	};
	struct args_kw akw[] = { [kw_required] = { "required", tc_required_kw },
		[kw_disabler] = { "disabler", obj_bool },
		[kw_modules] = { "modules", TYPE_TAG_LISTIFY | obj_string },
		[kw_pure] = { "pure", obj_bool },
		0 };
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	bool pure = false;
	if (akw[kw_pure].set) {
		pure = get_obj_bool(wk, akw[kw_pure].val);
	}

	enum requirement_type requirement;
	if (!coerce_requirement(wk, &akw[kw_required], &requirement)) {
		return false;
	}

	bool disabler = akw[kw_disabler].set && get_obj_bool(wk, akw[kw_disabler].val);

	const char *cmd = "python3";
	if (an[0].set) {
		const char *pycmd = get_cstr(wk, an[0].val);
		if (pycmd && *pycmd) {
			cmd = pycmd;
		}
	}

	TSTR(cmd_path);
	bool found = fs_find_cmd(wk, &cmd_path, cmd);
	if (!found && (requirement == requirement_required)) {
		vm_error(wk, "%s not found", cmd);
		return false;
	}

	if (!found && disabler) {
		*res = obj_disabler;
		return true;
	}

	if (!found) {
		return build_python_installation(wk, self, res, cmd_path, found, pure);
	}

	if (akw[kw_modules].set && found) {
		bool all_present = obj_array_foreach(wk,
			akw[kw_modules].val,
			&(struct iter_mod_ctx){
				.pythonpath = cmd_path.buf,
				.node = akw[kw_modules].node,
				.requirement = requirement,
			},
			iterate_required_module_list);

		if (!all_present) {
			if (requirement == requirement_required) {
				return false;
			}
			if (disabler) {
				*res = obj_disabler;
				return true;
			}
			/* Return a not-found object. */
			found = false;
		}
	}

	return build_python_installation(wk, self, res, cmd_path, found, pure);
}

static bool
func_python_installation_language_version(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_python_installation(wk, self)->language_version;
	return true;
}

static bool
func_module_python3_find_python(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const char *cmd = "python3";
	if (an[0].set) {
		cmd = get_cstr(wk, an[0].val);
	}

	TSTR(cmd_path);
	if (!fs_find_cmd(wk, &cmd_path, cmd)) {
		vm_error(wk, "python3 not found");
		return false;
	}

	*res = make_obj(wk, obj_external_program);
	struct obj_external_program *ep = get_obj_external_program(wk, *res);
	ep->found = true;
	ep->cmd_array = make_obj(wk, obj_array);
	obj_array_push(wk, ep->cmd_array, tstr_into_str(wk, &cmd_path));

	return true;
}

static bool
func_python_installation_get_path(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_string, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj path = an[0].val;
	obj sysconfig_paths = get_obj_python_installation(wk, self)->sysconfig_paths;
	if (obj_dict_index(wk, sysconfig_paths, path, res)) {
		return true;
	}

	if (!an[1].set) {
		vm_error(wk, "path '%o' not found, no default specified", path);
		return false;
	}

	*res = an[1].val;
	return true;
}

static bool
func_python_installation_get_var(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_string, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj var = an[0].val;
	obj sysconfig_vars = get_obj_python_installation(wk, self)->sysconfig_vars;
	if (obj_dict_index(wk, sysconfig_vars, var, res)) {
		return true;
	}

	if (!an[1].set) {
		vm_error(wk, "variable '%o' not found, no default specified", var);
		return false;
	}

	*res = an[1].val;
	return true;
}

static bool
func_python_installation_has_path(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj sysconfig_paths = get_obj_python_installation(wk, self)->sysconfig_paths;
	bool found = obj_dict_in(wk, sysconfig_paths, an[0].val);
	*res = make_obj_bool(wk, found);

	return true;
}

static bool
func_python_installation_has_var(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj sysconfig_vars = get_obj_python_installation(wk, self)->sysconfig_vars;
	bool found = obj_dict_in(wk, sysconfig_vars, an[0].val);
	*res = make_obj_bool(wk, found);

	return true;
}

static bool
get_install_dir(struct workspace *wk, obj self, bool pure, const char *subdir, obj *res)
{
	TSTR(installdir);

	obj prefix;
	get_option_value(wk, current_project(wk), "prefix", &prefix);

	struct obj_python_installation *py = get_obj_python_installation(wk, self);

	if (pure) {
		obj puredir;
		get_option_value(wk, current_project(wk), "python.purelibdir", &puredir);
		if (!str_eql(get_str(wk, puredir), &STR(""))) {
			path_push(wk, &installdir, get_cstr(wk, puredir));
		} else {
			if (!obj_dict_index_str(wk, py->install_paths, "purelib", &puredir)) {
				return false;
			}
			path_join_absolute(wk, &installdir, get_cstr(wk, prefix), get_cstr(wk, puredir));
		}
	} else {
		obj platdir;
		get_option_value(wk, current_project(wk), "python.platlibdir", &platdir);
		if (!str_eql(get_str(wk, platdir), &STR(""))) {
			path_push(wk, &installdir, get_cstr(wk, platdir));
		} else {
			if (!obj_dict_index_str(wk, py->install_paths, "platlib", &platdir)) {
				return false;
			}
			path_join_absolute(wk, &installdir, get_cstr(wk, prefix), get_cstr(wk, platdir));
		}
	}

	if (subdir) {
		path_push(wk, &installdir, subdir);
	}

	*res = tstr_into_str(wk, &installdir);

	return true;
}

static bool
func_python_installation_get_install_dir(struct workspace *wk, obj self, obj *res)
{
	enum kwargs {
		kw_pure,
		kw_subdir,
	};
	struct args_kw akw[] = {
		[kw_pure] = { "pure", obj_bool },
		[kw_subdir] = { "subdir", obj_string },
		0,
	};

	if (!pop_args(wk, NULL, akw)) {
		return false;
	}

	struct obj_python_installation *py = get_obj_python_installation(wk, self);
	bool pure = py->pure;
	if (akw[kw_pure].set) {
		pure = get_obj_bool(wk, akw[kw_pure].val);
	}

	const char *subdir = NULL;
	if (akw[kw_subdir].set) {
		subdir = get_cstr(wk, akw[kw_subdir].val);
	}

	return get_install_dir(wk, self, pure, subdir, res);
}

struct py_install_data_rename_ctx {
	obj rename;
	obj mode;
	obj dest;
	uint32_t i;
	uint32_t node;
};

static enum iteration_result
py_install_data_rename_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct py_install_data_rename_ctx *ctx = _ctx;

	obj src = *get_obj_file(wk, val);
	obj dest;

	obj rename;
	rename = obj_array_index(wk, ctx->rename, ctx->i);

	TSTR(d);
	path_join(wk, &d, get_cstr(wk, ctx->dest), get_cstr(wk, rename));

	dest = tstr_into_str(wk, &d);

	push_install_target(wk, src, dest, ctx->mode);

	++ctx->i;
	return ir_cont;
}

static bool
func_python_installation_install_sources(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | tc_file | tc_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_follow_symlinks,
		kw_install_dir,
		kw_install_mode,
		kw_install_tag,
		kw_rename,
		kw_sources,
		kw_preserve_path,
		kw_pure,
		kw_subdir,
	};

	struct args_kw akw[] = {
		[kw_follow_symlinks] = { "follow_symlinks", obj_bool }, // TODO
		[kw_install_dir] = { "install_dir", obj_string },
		[kw_install_mode] = { "install_mode", tc_install_mode_kw },
		[kw_install_tag] = { "install_tag", obj_string }, // TODO
		[kw_rename] = { "rename", TYPE_TAG_LISTIFY | obj_string },
		[kw_sources] = { "sources", TYPE_TAG_LISTIFY | tc_file | tc_string },
		[kw_preserve_path] = { "preserve_path", obj_bool },
		[kw_pure] = { "pure", obj_bool },
		[kw_subdir] = { "subdir", obj_string },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (akw[kw_rename].set && akw[kw_preserve_path].set) {
		vm_error(wk, "rename keyword conflicts with preserve_path");
		return false;
	}

	struct obj_python_installation *py = get_obj_python_installation(wk, self);

	bool pure = py->pure;
	if (akw[kw_pure].set) {
		pure = get_obj_bool(wk, akw[kw_pure].val);
	}

	const char *subdir = NULL;
	if (akw[kw_subdir].set) {
		subdir = get_cstr(wk, akw[kw_subdir].val);
	}

	obj install_dir;
	if (akw[kw_install_dir].set) {
		install_dir = akw[kw_install_dir].val;
	} else {
		get_install_dir(wk, self, pure, subdir, &install_dir);
	}

	obj sources = an[0].val;
	uint32_t err_node = an[0].node;

	if (akw[kw_sources].set) {
		obj_array_extend_nodup(wk, sources, akw[kw_sources].val);
		err_node = akw[kw_sources].node;
	}

	if (akw[kw_rename].set) {
		if (get_obj_array(wk, akw[kw_rename].val)->len != get_obj_array(wk, sources)->len) {
			vm_error(wk, "number of elements in rename != number of sources");
			return false;
		}

		struct py_install_data_rename_ctx ctx = {
			.node = err_node,
			.mode = akw[kw_install_mode].val,
			.rename = akw[kw_rename].val,
			.dest = install_dir,
		};

		obj coerced;
		if (!coerce_files(wk, err_node, sources, &coerced)) {
			return false;
		}

		return obj_array_foreach(wk, coerced, &ctx, py_install_data_rename_iter);
	}

	bool preserve_path = akw[kw_preserve_path].set && get_obj_bool(wk, akw[kw_preserve_path].val);

	return push_install_targets(wk, err_node, sources, install_dir, akw[kw_install_mode].val, preserve_path);
}

static bool
func_python_installation_interpreter_path(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct obj_python_installation *py = get_obj_python_installation(wk, self);
	struct obj_external_program *ep = get_obj_external_program(wk, py->prog);
	if (get_obj_array(wk, ep->cmd_array)->len > 1) {
		vm_error(wk,
			"cannot return the full_path() of an external program with multiple elements (have: %o)\n",
			ep->cmd_array);
		return false;
	}

	*res = obj_array_index(wk, get_obj_external_program(wk, self)->cmd_array, 0);
	return true;
}

static bool
func_python_installation_dependency(struct workspace *wk, obj self, obj *res)
{
	struct arr kwargs;
	func_kwargs_lookup(wk, 0, "dependency", &kwargs);
	kwargs_arr_push(wk, &kwargs, &(struct args_kw){ "embed", obj_bool });

	if (!pop_args(wk, 0, (struct args_kw *)kwargs.e)) {
		kwargs_arr_destroy(wk, &kwargs);
		return false;
	}
	kwargs_arr_destroy(wk, &kwargs);

	vm_error(wk, "unimplemented");
	return false;
}

static bool
func_python_installation_extension_module(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ obj_string },
		{ TYPE_TAG_GLOB | tc_coercible_files | tc_generated_list },
		ARG_TYPE_NULL,
	};

	struct arr kwargs;
	func_kwargs_lookup(wk, 0, "shared_module", &kwargs);
	kwargs_arr_del(wk, &kwargs, "name_suffix");
	kwargs_arr_del(wk, &kwargs, "name_prefix");
	kwargs_arr_push(wk, &kwargs, &(struct args_kw){ "subdir", obj_string });
	kwargs_arr_push(wk, &kwargs, &(struct args_kw){ "limited_api", obj_string });

	if (!pop_args(wk, an, (struct args_kw *)kwargs.e)) {
		kwargs_arr_destroy(wk, &kwargs);
		return false;
	}
	kwargs_arr_destroy(wk, &kwargs);

	vm_error(wk, "unimplemented");
	return false;
}

static obj
python_self_transform(struct workspace *wk, obj self)
{
	return get_obj_python_installation(wk, self)->prog;
}

void
python_build_impl_tbl(void)
{
	uint32_t i;
	for (i = 0; impl_tbl_external_program[i].name; ++i) {
		struct func_impl tmp = impl_tbl_external_program[i];
		tmp.self_transform = python_self_transform;
		impl_tbl_python_installation[i] = tmp;
	}
}

const struct func_impl impl_tbl_module_python[] = {
	{ "find_installation", func_module_python_find_installation, tc_python_installation },
	{ NULL, NULL },
};

const struct func_impl impl_tbl_module_python3[] = {
	{ "find_python", func_module_python3_find_python, tc_external_program },
	{ NULL, NULL },
};

struct func_impl impl_tbl_python_installation[] = {
	[ARRAY_LEN(impl_tbl_external_program) - 1] = { "get_path", func_python_installation_get_path, tc_string },
	{ "get_install_dir", func_python_installation_get_install_dir, tc_string },
	{ "get_variable", func_python_installation_get_var, tc_string },
	{ "has_path", func_python_installation_has_path, tc_bool },
	{ "has_variable", func_python_installation_has_var, tc_bool },
	{ "install_sources", func_python_installation_install_sources },
	{ "language_version", func_python_installation_language_version, tc_string },
	{ "path", func_python_installation_interpreter_path, tc_string },
	{ "dependency", func_python_installation_dependency, tc_dependency },
	{ "extension_module", func_python_installation_extension_module, tc_build_target },
	{ NULL, NULL },
};
