/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/modules/python.h"
#include "functions/external_program.h"
#include "lang/interpreter.h"
#include "platform/filesystem.h"
#include "platform/run_cmd.h"

static const char *introspect_program =
	"import sysconfig\n"
	"print(sysconfig.get_python_version())\n";

static bool
introspect_python_interpreter(struct workspace *wk, obj *res, const char *path,
	struct obj_python_installation *python)
{
	struct run_cmd_ctx cmd_ctx = { 0 };
	char *const args[] = { (char *)path, "-c", (char *)introspect_program, 0 };
	if (!run_cmd_argv(&cmd_ctx, args, NULL, 0) || cmd_ctx.status != 0)
		return false;

	size_t index = 0, buf_index = 0, max_buf_index = cmd_ctx.out.len;
	char *buf = cmd_ctx.out.buf, *entry = cmd_ctx.out.buf;
	while (buf_index != max_buf_index) {
		if (buf[buf_index] == '\n') {
			buf[buf_index] = '\0';

			if (index == 0) {  /* language_version */
				python->language_version = make_str(wk, entry);
			}

			entry = &buf[buf_index + 1];
			index++;
		} else {
			buf_index++;
		}
	}

	return buf[buf_index] == '\0';
}

static bool
func_module_python_find_python(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, NULL, ao, NULL)) {
		return false;
	}

	const char *cmd = "python3";
	if (ao[0].set) {
		cmd = get_cstr(wk, ao[0].val);
	}

	SBUF(cmd_path);
	if (!fs_find_cmd(wk, &cmd_path, cmd)) {
		interp_error(wk, args_node, "python3 not found");
		return false;
	}

	make_obj(wk, res, obj_python_installation);
	struct obj_python_installation *python = get_obj_python_installation(wk, *res);
	make_obj(wk, &python->prog, obj_external_program);
	struct obj_external_program *ep = get_obj_external_program(wk, python->prog);
	ep->found = true;
	make_obj(wk, &ep->cmd_array, obj_array);
	obj_array_push(wk, ep->cmd_array, sbuf_into_str(wk, &cmd_path));

	if (!introspect_python_interpreter(wk, res, cmd_path.buf, python)) {
		interp_error(wk, args_node, "failed to introspect python");
		return false;
	}

	return true;
}

static bool
python_module_present(struct workspace *wk, const char *pythonpath, const char *mod)
{
	struct run_cmd_ctx cmd_ctx = { 0 };

	SBUF(importstr);
	sbuf_pushf(wk, &importstr, "import %s", mod);

	char *const *args = (char *const []){
		(char *)pythonpath,
		"-c",
		importstr.buf,
		0
	};

	bool present = run_cmd_argv(&cmd_ctx, args, NULL, 0) && cmd_ctx.status == 0;

	run_cmd_ctx_destroy(&cmd_ctx);

	return present;
}

struct iter_mod_ctx {
	const char *pythonpath;
	uint32_t node;
	bool required;
};

static enum iteration_result
iterate_required_module_list(struct workspace *wk, void *ctx, obj val)
{
	struct iter_mod_ctx *_ctx = ctx;
	const char *mod = get_cstr(wk, val);

	if (python_module_present(wk, _ctx->pythonpath, mod)) {
		return ir_cont;
	}

	if (_ctx->required) {
		interp_error(wk, _ctx->node, "python: required module '%s' not found", mod);
	}

	return ir_err;
}

static bool
func_module_python_find_installation(struct workspace *wk,
	obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_disabler,
		kw_modules,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required", obj_bool },
		[kw_disabler] = { "disabler", obj_bool },
		[kw_modules] = { "modules", ARG_TYPE_ARRAY_OF | obj_string },
		0
	};
	if (!interp_args(wk, args_node, NULL, ao, akw)) {
		return false;
	}

	bool required = !akw[kw_required].set || get_obj_bool(wk, akw[kw_required].val);
	bool disabler = akw[kw_disabler].set && get_obj_bool(wk, akw[kw_disabler].val);

	const char *cmd = "python3";
	if (ao[0].set) {
		cmd = get_cstr(wk, ao[0].val);
	}

	SBUF(cmd_path);
	bool found = fs_find_cmd(wk, &cmd_path, cmd);
	if (required && !found) {
		interp_error(wk, args_node, "%s not found", cmd);
		return false;
	}

	if (!found && disabler) {
		*res = disabler_id;
		return true;
	}

	if (akw[kw_modules].set && found) {
		bool all_present = obj_array_foreach(wk,
			akw[kw_modules].val,
			&(struct iter_mod_ctx){
			.pythonpath = cmd_path.buf,
			.node = akw[kw_modules].node,
			.required = required,
		},
			iterate_required_module_list
			);

		if (!all_present) {
			if (required) {
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

	make_obj(wk, res, obj_python_installation);
	struct obj_python_installation *python = get_obj_python_installation(wk, *res);
	make_obj(wk, &python->prog, obj_external_program);
	struct obj_external_program *ep = get_obj_external_program(wk, python->prog);
	ep->found = found;
	make_obj(wk, &ep->cmd_array, obj_array);
	obj_array_push(wk, ep->cmd_array, sbuf_into_str(wk, &cmd_path));

	if (!introspect_python_interpreter(wk, res, cmd_path.buf, python)) {
		interp_error(wk, args_node, "failed to introspect python");
		return false;
	}

	return true;
}

static bool
func_python_installation_language_version(struct workspace *wk, obj rcvr,
	uint32_t args_node, obj *res)
{
	*res = get_obj_python_installation(wk, rcvr)->language_version;
	return true;
}

static obj
python_rcvr_transform(struct workspace *wk, obj rcvr)
{
	return get_obj_python_installation(wk, rcvr)->prog;
}

void
python_build_impl_tbl(void)
{
	uint32_t i;
	for (i = 0; impl_tbl_external_program[i].name; ++i) {
		struct func_impl_name tmp = impl_tbl_external_program[i];
		tmp.rcvr_transform = python_rcvr_transform;
		impl_tbl_module_python[i] = tmp;
		impl_tbl_module_python3[i] = tmp;
	}
}

struct func_impl_name impl_tbl_module_python[] = {
	[ARRAY_LEN(impl_tbl_external_program) - 1] =
	{ "find_installation", func_module_python_find_installation, tc_python_installation },
	{ NULL, NULL },
};

struct func_impl_name impl_tbl_module_python3[] = {
	[ARRAY_LEN(impl_tbl_external_program) - 1] =
	{ "find_python", func_module_python_find_python, tc_python_installation },
	{ NULL, NULL },
};

const struct func_impl_name impl_tbl_python_installation[] = {
	{ "language_version", func_python_installation_language_version, tc_string },
	{ NULL, NULL },
};
