/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "args.h"
#include "backend/backend.h"
#include "backend/ninja.h"
#include "backend/vs.h"
#include "functions/environment.h"
#include "lang/object_iterators.h"
#include "log.h"
#include "platform/init.h"
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

static void
backend_print_stack(struct workspace *wk)
{
	log_plain("stack trace:\n");
	obj v;
	obj_array_for(wk, wk->backend_output_stack, v) {
		log_plain(" -> %s\n", get_cstr(wk, v));
	}
}

static void
backend_abort_handler(void *_ctx)
{
	struct workspace *wk = _ctx;
	LOG_E("an unhandled error occured during backend output");
	backend_print_stack(wk);
}

bool
backend_output(struct workspace *wk, enum backend_output backend)
{
	TracyCZoneAutoS;

	make_obj(wk, &wk->backend_output_stack, obj_array);
	platform_set_abort_handler(backend_abort_handler, wk);

	switch (backend) {
	case backend_output_ninja:
		if (!ninja_write_all(wk)) {
			LOG_E("backend output failed");

			backend_print_stack(wk);
			TracyCZoneAutoE;
			return false;
		}
		break;
	case backend_output_vs:
	case backend_output_vs2019:
	case backend_output_vs2022:
		if (!vs_write_all(wk, backend)) {
			LOG_E("backend output failed");

			backend_print_stack(wk);
			TracyCZoneAutoE;
			return false;
		}
		break;
	}

	if (!obj_array_foreach(wk, wk->postconf_scripts, NULL, run_postconf_script_iter)) {
		TracyCZoneAutoE;
		return false;
	}

	TracyCZoneAutoE;
	return true;
}
