#include "posix.h"

#include "args.h"
#include "backend/backend.h"
#include "backend/ninja.h"
#include "functions/environment.h"
#include "log.h"
#include "platform/run_cmd.h"

static enum iteration_result
run_postconf_script_iter(struct workspace *wk, void *_ctx, obj arr)
{
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
		goto err;
	}

	if (cmd_ctx.status != 0) {
		LOG_E("postconf script failed");
		LOG_E("stdout: %s", cmd_ctx.out.buf);
		LOG_E("stderr: %s", cmd_ctx.err.buf);
		goto err;
	}

	run_cmd_ctx_destroy(&cmd_ctx);
	return ir_cont;
err:
	run_cmd_ctx_destroy(&cmd_ctx);
	return ir_err;
}

bool
backend_output(struct workspace *wk)
{
	if (!ninja_write_all(wk)) {
		return false;
	}

	if (!obj_array_foreach(wk, wk->postconf_scripts, NULL, run_postconf_script_iter)) {
		return false;
	}

	return true;
}
