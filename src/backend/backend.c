/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include "args.h"
#include "backend/backend.h"
#include "backend/ninja.h"
#include "functions/environment.h"
#include "log.h"
#include "platform/run_cmd.h"
#include "tracy.h"

static enum iteration_result
run_postconf_script_iter(struct workspace *wk, void *_ctx, obj arr)
{
	TracyCZoneAutoS;
	enum iteration_result ret = ir_err;
	obj env;
	make_obj(wk, &env, obj_dict);
	set_default_environment_vars(wk, env, false);

	const char *argstr, *envstr;
	uint32_t argc, envc;
	env_to_envstr(wk, &envstr, &envc, env);
	join_args_argstr(wk, &argstr, &argc, arr);

	LOG_I("running postconf script '%s'", argstr);

	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd(&cmd_ctx, argstr, argc, envstr, envc)) {
		LOG_E("failed to run postconf script: %s", cmd_ctx.err_msg);
		goto ret;
	}

	if (cmd_ctx.status != 0) {
		LOG_E("postconf script failed");
		LOG_E("stdout: %s", cmd_ctx.out.buf);
		LOG_E("stderr: %s", cmd_ctx.err.buf);
		goto ret;
	}

	ret = ir_cont;
ret:
	run_cmd_ctx_destroy(&cmd_ctx);
	TracyCZoneAutoE;
	return ret;
}

bool
backend_output(struct workspace *wk)
{
	TracyCZoneAutoS;
	if (!ninja_write_all(wk)) {
		LOG_E("backend output failed");
		TracyCZoneAutoE;
		return false;
	}

	if (!obj_array_foreach(wk, wk->postconf_scripts, NULL, run_postconf_script_iter)) {
		TracyCZoneAutoE;
		return false;
	}

	TracyCZoneAutoE;
	return true;
}
