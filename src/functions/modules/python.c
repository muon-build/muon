/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "coerce.h"
#include "compat.h"

#include "lang/object.h"
#include "options.h"
#include "platform/path.h"

#include "embedded.h"
#include "external/tinyjson.h"
#include "functions/external_program.h"
#include "functions/modules/python.h"
#include "install.h"
#include "lang/typecheck.h"
#include "platform/filesystem.h"
#include "platform/run_cmd.h"

static bool
introspect_python_interpreter(struct workspace *wk, const char *path, struct obj_python_installation *python)
{
	const char *pyinfo = embedded_get("python_info.py");
	if (!pyinfo) {
		return false;
	}

	struct run_cmd_ctx cmd_ctx = { 0 };
	char *const var_args[] = { (char *)path, "-c", (char *)pyinfo, 0 };
	if (!run_cmd_argv(&cmd_ctx, var_args, NULL, 0) || cmd_ctx.status != 0) {
		return false;
	}

	obj res_introspect;
	bool success = muon_json_to_dict(wk, cmd_ctx.out.buf, &res_introspect);

	if (!success) {
		goto end;
	}

	if (!obj_dict_index_str(wk, res_introspect, "version", &python->language_version)) {
		success = false;
		goto end;
	}

	if (!obj_dict_index_str(wk, res_introspect, "sysconfig_paths", &python->sysconfig_paths)) {
		success = false;
		goto end;
	}

	if (!obj_dict_index_str(wk, res_introspect, "variables", &python->sysconfig_vars)) {
		success = false;
		goto end;
	}

	if (!obj_dict_index_str(wk, res_introspect, "install_paths", &python->install_paths)) {
		success = false;
		goto end;
	}
end:
	run_cmd_ctx_destroy(&cmd_ctx);
	return success;
}

static bool
python_module_present(struct workspace *wk, const char *pythonpath, const char *mod)
{
	struct run_cmd_ctx cmd_ctx = { 0 };

	SBUF(importstr);
	sbuf_pushf(wk, &importstr, "import %s", mod);

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
build_python_installation(struct workspace *wk, obj self, obj *res, struct sbuf cmd_path, bool found, bool pure)
{
	make_obj(wk, res, obj_python_installation);
	struct obj_python_installation *python = get_obj_python_installation(wk, *res);
	python->pure = pure;
	make_obj(wk, &python->prog, obj_external_program);
	struct obj_external_program *ep = get_obj_external_program(wk, python->prog);
	ep->found = found;
	make_obj(wk, &ep->cmd_array, obj_array);
	obj_array_push(wk, ep->cmd_array, sbuf_into_str(wk, &cmd_path));

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

	SBUF(cmd_path);
	bool found = fs_find_cmd(wk, &cmd_path, cmd);
	if (!found && (requirement == requirement_required)) {
		vm_error(wk, "%s not found", cmd);
		return false;
	}

	if (!found && disabler) {
		*res = disabler_id;
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
				*res = disabler_id;
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

	SBUF(cmd_path);
	if (!fs_find_cmd(wk, &cmd_path, cmd)) {
		vm_error(wk, "python3 not found");
		return false;
	}

	make_obj(wk, res, obj_external_program);
	struct obj_external_program *ep = get_obj_external_program(wk, *res);
	ep->found = true;
	make_obj(wk, &ep->cmd_array, obj_array);
	obj_array_push(wk, ep->cmd_array, sbuf_into_str(wk, &cmd_path));

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
	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, found);

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
	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, found);

	return true;
}

static bool
get_install_dir(struct workspace *wk, obj self, bool pure, const char *subdir, obj *res)
{
	SBUF(installdir);

	obj prefix;
	get_option_value(wk, current_project(wk), "prefix", &prefix);

	struct obj_python_installation *py = get_obj_python_installation(wk, self);

	if (pure) {
		obj puredir;
		get_option_value(wk, current_project(wk), "python.purelibdir", &puredir);
		if (!str_eql(get_str(wk, puredir), &WKSTR(""))) {
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
		if (!str_eql(get_str(wk, platdir), &WKSTR(""))) {
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

	*res = sbuf_into_str(wk, &installdir);

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
		0
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
	{ "language_version", func_python_installation_language_version, tc_string },
	{ NULL, NULL },
};
