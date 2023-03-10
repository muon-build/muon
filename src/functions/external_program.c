/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/common.h"
#include "functions/external_program.h"
#include "guess.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/run_cmd.h"

void
find_program_guess_version(struct workspace *wk, const char *path, obj *ver)
{
	*ver = 0;
	struct run_cmd_ctx cmd_ctx = { 0 };
	if (run_cmd_argv(&cmd_ctx, (char *const []){ (char *)path, "--version", 0 }, NULL, 0)
	    && cmd_ctx.status == 0) {
		guess_version(wk, cmd_ctx.out.buf, ver);
	}

	run_cmd_ctx_destroy(&cmd_ctx);
}


static bool
func_external_program_found(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, get_obj_external_program(wk, rcvr)->found);
	return true;
}

static bool
func_external_program_path(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_external_program(wk, rcvr)->full_path;
	return true;
}

static bool
func_external_program_version(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	struct obj_external_program *prog = get_obj_external_program(wk, rcvr);
	if (!prog->guessed_ver) {
		find_program_guess_version(wk, get_cstr(wk, prog->full_path), &prog->ver);
		prog->guessed_ver = true;
	}

	*res = prog->ver;
	return true;
}

const struct func_impl_name impl_tbl_external_program[] = {
	{ "found", func_external_program_found, tc_bool },
	{ "path", func_external_program_path, tc_string },
	{ "full_path", func_external_program_path, tc_string },
	{ "version", func_external_program_version, tc_string },
	{ NULL, NULL },
};
