/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "args.h"
#include "functions/external_program.h"
#include "guess.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/run_cmd.h"

void
find_program_guess_version(struct workspace *wk, obj cmd_array, obj version_argument, obj *ver)
{
	*ver = 0;

	struct run_cmd_ctx cmd_ctx = { 0 };
	obj args;
	obj_array_dup(wk, cmd_array, &args);
	obj_array_push(wk, args, version_argument ? version_argument : make_str(wk, "--version"));

	const char *argstr;
	uint32_t argc;
	join_args_argstr(wk, &argstr, &argc, args);

	if (run_cmd(wk, &cmd_ctx, argstr, argc, NULL, 0) && cmd_ctx.status == 0) {
		if (!guess_version(wk, cmd_ctx.out.buf, ver)) {
			*ver = make_str(wk, "unknown");
		}
	}

	run_cmd_ctx_destroy(&cmd_ctx);
}

FUNC_IMPL(external_program, found, tc_bool, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, get_obj_external_program(wk, self)->found);
	return true;
}

FUNC_IMPL(external_program, full_path, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct obj_external_program *ep = get_obj_external_program(wk, self);

	if (ep->original_argv0) {
		*res = ep->original_argv0;
		return true;
	}

	if (get_obj_array(wk, ep->cmd_array)->len > 1) {
		vm_error(wk,
			"cannot return the full_path() of an external program with multiple elements (have: %o)\n",
			ep->cmd_array);
		return false;
	}

	*res = obj_array_index(wk, get_obj_external_program(wk, self)->cmd_array, 0);
	return true;
}

FUNC_IMPL(external_program, version, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct obj_external_program *prog = get_obj_external_program(wk, self);
	if (!prog->guessed_ver) {
		find_program_guess_version(wk, prog->cmd_array, 0, &prog->ver);
		prog->guessed_ver = true;
	}

	*res = prog->ver;
	return true;
}

FUNC_REGISTER(external_program)
{
	FUNC_IMPL_REGISTER(external_program, found);
	FUNC_IMPL_REGISTER(external_program, full_path);
	FUNC_IMPL_REGISTER_ALIAS(external_program, full_path, path);
	FUNC_IMPL_REGISTER(external_program, version);
}
