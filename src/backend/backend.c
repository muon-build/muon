/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "args.h"
#include "backend/backend.h"
#include "backend/common_args.h"
#include "backend/introspect.h"
#include "backend/ninja.h"
#include "backend/output.h"
#include "backend/xcode.h"
#include "functions/environment.h"
#include "lang/object_iterators.h"
#include "lang/serial.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/init.h"
#include "platform/run_cmd.h"
#include "tracy.h"

static enum iteration_result
run_postconf_script_iter(struct workspace *wk, void *_ctx, obj arr)
{
	TracyCZoneAutoS;
	enum iteration_result ret = ir_err;
	obj env;
	env = make_obj(wk, obj_dict);
	set_default_environment_vars(wk, env, false);

	const char *argstr, *envstr;
	uint32_t argc, envc;
	env_to_envstr(wk, &envstr, &envc, env);
	join_args_argstr(wk, &argstr, &argc, arr);

	LOG_I("running postconf script '%s'", argstr);

	struct run_cmd_ctx cmd_ctx = {
		.chdir = wk->build_root,
	};
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
	log_plain(log_info, "stack trace:\n");
	obj v;
	obj_array_for(wk, wk->backend_output_stack, v) {
		log_plain(log_info, " -> %s\n", get_cstr(wk, v));
	}
}

static void
backend_abort_handler(void *_ctx)
{
	struct workspace *wk = _ctx;
	LOG_E("an unhandled error occured during backend output");
	backend_print_stack(wk);
}

static bool
write_tests(struct workspace *wk, void *_ctx, FILE *out)
{
	bool wrote_header = false;

	obj tests;
	tests = make_obj(wk, obj_dict);

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = arr_get(&wk->projects, i);
		if (proj->not_ok) {
			continue;
		}

		if (proj->tests && get_obj_array(wk, proj->tests)->len) {
			if (!wrote_header) {
				L("writing tests");
				wrote_header = true;
			}

			obj res, key;
			key = proj->cfg.name;

			if (obj_dict_index(wk, tests, key, &res)) {
				assert(false && "project defined multiple times");
			}

			obj arr;
			arr = make_obj(wk, obj_array);

			obj_array_push(wk, arr, proj->tests);
			obj_array_push(wk, arr, proj->test_setups);
			obj_array_push(wk, arr, make_number(wk, i));
			obj_dict_set(wk, tests, key, arr);
		}
	}

	return serial_dump(wk, tests, out);
}

static bool
write_install(struct workspace *wk, void *_ctx, FILE *out)
{
	obj o;
	o = make_obj(wk, obj_array);
	obj_array_push(wk, o, wk->install);
	obj_array_push(wk, o, wk->install_scripts);
	obj_array_push(wk, o, make_str(wk, wk->source_root));

	struct project *proj = arr_get(&wk->projects, 0);
	obj prefix;
	get_option_value(wk, proj, "prefix", &prefix);
	obj_array_push(wk, o, prefix);

	return serial_dump(wk, o, out);
}

static bool
write_compiler_check_cache(struct workspace *wk, void *_ctx, FILE *out)
{
	return serial_dump(wk, wk->compiler_check_cache, out);
}

static bool
write_summary_file(struct workspace *wk, void *_ctx, FILE *out)
{
	workspace_print_summaries(wk, out);
	return true;
}

static bool
write_option_info(struct workspace *wk, void *_ctx, FILE *out)
{
	obj arr;
	arr = make_obj(wk, obj_array);
	obj_array_push(wk, arr, wk->global_opts);

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = arr_get(&wk->projects, i);
		obj_array_push(wk, arr, proj->opts);
		obj_array_push(wk, arr, proj->cfg.name);
	}

	return serial_dump(wk, arr, out);
}

bool
backend_output(struct workspace *wk)
{
	TracyCZoneAutoS;

	wk->backend_output_stack = make_obj(wk, obj_array);
	platform_set_abort_handler(backend_abort_handler, wk);

	bool ok = true;

	if (!ca_prepare_all_targets(wk)) {
		ok = false;
	}

	{
		double total = 0;
		for (uint32_t i = 0; i < wk->projects.len; ++i) {
			struct project *proj = arr_get(&wk->projects, i);
			total += get_obj_array(wk, proj->targets)->len;
		}
		log_progress_set_style(&(struct log_progress_style) { .name = "backend", .name_pad = 20 });
		log_progress_push_level(0, total);
	}

	if (ok) {
		switch (get_option_backend(wk)) {
		case backend_ninja: ok = ninja_write_all(wk); break;
		case backend_xcode: ok = ninja_write_all(wk) && xcode_write_all(wk); break;
		}
	}

	if (ok) {
		ok = with_open(wk->muon_private, output_path.tests, wk, NULL, write_tests)
		     && with_open(wk->muon_private, output_path.install, wk, NULL, write_install)
		     && with_open(
			     wk->muon_private, output_path.compiler_check_cache, wk, NULL, write_compiler_check_cache)
		     && with_open(wk->muon_private, output_path.summary, wk, NULL, write_summary_file)
		     && with_open(wk->muon_private, output_path.option_info, wk, NULL, write_option_info)
			 && introspect_write_all(wk);
	}

	if (!ok) {
		LOG_E("backend output failed");

		backend_print_stack(wk);
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
