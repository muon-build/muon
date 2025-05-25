/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "backend/ninja.h"
#include "backend/ninja/alias_target.h"
#include "backend/ninja/build_target.h"
#include "backend/ninja/coverage.h"
#include "backend/ninja/custom_target.h"
#include "backend/ninja/rules.h"
#include "backend/output.h"
#include "error.h"
#include "external/samurai.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "tracy.h"

struct check_tgt_ctx {
	bool need_phony;
};

static enum iteration_result
check_tgt_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct check_tgt_ctx *ctx = _ctx;

	enum obj_type t = get_obj_type(wk, tgt_id);
	switch (t) {
	case obj_custom_target: {
		struct obj_custom_target *tgt = get_obj_custom_target(wk, tgt_id);

		if (tgt->flags & custom_target_build_always_stale) {
			ctx->need_phony = true;
		}
		break;
	}
	case obj_alias_target:
	case obj_build_target:
	case obj_both_libs: break;
	default: UNREACHABLE;
	}

	return ir_cont;
}

static enum iteration_result
write_tgt_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	TracyCZoneAutoS;
	enum iteration_result ret;
	struct write_tgt_ctx *ctx = _ctx;

	struct obj_clear_mark mk;
	obj_set_clear_mark(wk, &mk);

	enum obj_type t = get_obj_type(wk, tgt_id);
	const char *name = get_cstr(wk, ca_backend_tgt_name(wk, tgt_id));

	obj_array_push(wk, wk->backend_output_stack, make_strf(wk, "writing target %s", name));

	switch (t) {
	case obj_alias_target: ret = ninja_write_alias_tgt(wk, tgt_id, ctx); break;
	case obj_build_target: ret = ninja_write_build_tgt(wk, tgt_id, ctx); break;
	case obj_custom_target: ret = ninja_write_custom_tgt(wk, tgt_id, ctx); break;
	default: UNREACHABLE;
	}

	if (!ret) {
		LOG_E("failed to write %s '%s'", obj_type_to_s(t), name);
	}

	obj_array_pop(wk, wk->backend_output_stack);

	obj_clear(wk, &mk);

	TracyCZoneAutoE;
	log_progress_inc(wk);
	return ret;
}

struct write_build_ctx {
	obj compiler_rule_arr;
};

static bool
ninja_write_build(struct workspace *wk, void *_ctx, FILE *out)
{
	struct write_build_ctx *ctx = _ctx;
	struct check_tgt_ctx check_ctx = { 0 };

	bool coverage_enabled = ninja_coverage_is_enabled_and_available(wk);
	check_ctx.need_phony = coverage_enabled;

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = arr_get(&wk->projects, i);
		if (proj->not_ok) {
			continue;
		}

		obj_array_foreach(wk, proj->targets, &check_ctx, check_tgt_iter);
	}

	if (!ninja_write_rules(out, wk, arr_get(&wk->projects, 0), check_ctx.need_phony, ctx->compiler_rule_arr)) {
		return false;
	}

	bool wrote_default = false;

	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = arr_get(&wk->projects, i);
		if (proj->not_ok) {
			continue;
		}

		struct write_tgt_ctx ctx = { .out = out, .proj = proj };

		if (!obj_array_foreach(wk, proj->targets, &ctx, write_tgt_iter)) {
			LOG_E("failed to write rules for project %s", get_cstr(wk, proj->cfg.name));
			return false;
		}

		wrote_default |= ctx.wrote_default;
	}

	if (coverage_enabled) {
		ninja_coverage_write_targets(wk, out);
	}

	{ // Add install target for compatibility with meson
		fprintf(out,
			"build install: phony muon-internal__install\n"
			"build muon-internal__install: CUSTOM_COMMAND\n"
			" desc = Installing$ files\n"
			" COMMAND = %s install\n"
			" pool = console\n\n",
			wk->argv0);
	}

	if (!wrote_default) {
		fprintf(out,
			"build muon_do_nothing: phony\n"
			"default muon_do_nothing\n");
	}

	return true;
}

bool
ninja_write_all(struct workspace *wk)
{
	struct write_build_ctx ctx = { 0 };
	ctx.compiler_rule_arr = make_obj(wk, obj_array);

	obj_array_push(wk, wk->backend_output_stack, make_str(wk, "ninja_write_all"));

	if (!(with_open(wk->build_root, "build.ninja", wk, &ctx, ninja_write_build))) {
		return false;
	}

	obj_array_pop(wk, wk->backend_output_stack);

	{
		TSTR(ninja_log);
		path_join(wk, &ninja_log, wk->build_root, output_path.private_dir);
		path_push(wk, &ninja_log, ".ninja_log");

		if (fs_file_exists(ninja_log.buf)) {
			obj args = make_obj(wk, obj_array);
			obj_array_push(wk, args, make_str(wk, "-t"));
			obj_array_push(wk, args, make_str(wk, "restat"));
			obj_array_push(wk, args, make_str(wk, "build.ninja"));

			ninja_run(
				wk, args, wk->build_root, 0, ninja_run_flag_ignore_errors | ninja_run_flag_prefer_ninja);
		}
	}

	{ /* compile_commands.json */
		TracyCZoneN(tctx_compdb, "output compile_commands.json", true);

		obj compdb_args;
		compdb_args = make_obj(wk, obj_array);
		obj_array_push(wk, compdb_args, make_str(wk, "-C"));
		obj_array_push(wk, compdb_args, make_str(wk, wk->build_root));
		obj_array_push(wk, compdb_args, make_str(wk, "-t"));
		obj_array_push(wk, compdb_args, make_str(wk, "compdb"));
		obj_array_extend_nodup(wk, compdb_args, ctx.compiler_rule_arr);
		if (!ninja_run(wk, compdb_args, wk->build_root, "compile_commands.json", 0)) {
			LOG_E("error writing compile_commands.json");
		}

		TracyCZoneEnd(tctx_compdb);
	}

	return true;
}

bool
ninja_run(struct workspace *wk, obj args, const char *chdir, const char *capture, enum ninja_run_flag flags)
{
	const char *argstr;
	uint32_t argstr_argc;

	bool ret = false;
	char *const *argv = NULL;
	uint32_t argc;
	TSTR_manual(cwd);

	if (chdir) {
		path_copy_cwd(NULL, &cwd);

		if (!path_chdir(chdir)) {
			goto ret;
		}
	}

	if (have_samurai && !(flags & ninja_run_flag_prefer_ninja)) {
		join_args_argstr(wk, &argstr, &argstr_argc, args);
		argc = argstr_to_argv(argstr, argstr_argc, "samu", &argv);

		struct samu_opts samu_opts = { .out = stdout };
		if (capture) {
			if (!(samu_opts.out = fs_fopen(capture, "wb"))) {
				goto ret;
			}
		}

		ret = samu_main(argc, (char **)argv, &samu_opts);

		if (capture) {
			if (!fs_fclose(samu_opts.out)) {
				goto ret;
			}
		}
	} else {
		struct run_cmd_ctx cmd_ctx = { 0 };

		TSTR_manual(cmd);
		const char *prepend = NULL;

		obj cmd_arr;
		{
			obj ninja_opt;
			get_option_value(wk, NULL, "env.NINJA", &ninja_opt);

			obj_array_dup(wk, ninja_opt, &cmd_arr);
			obj_array_extend_nodup(wk, cmd_arr, args);
			args = cmd_arr;
		}

		join_args_argstr(wk, &argstr, &argstr_argc, args);
		argc = argstr_to_argv(argstr, argstr_argc, prepend, &argv);

		if (!capture && !(flags & ninja_run_flag_ignore_errors)) {
			cmd_ctx.flags |= run_cmd_ctx_flag_dont_capture;
		}

		if (!run_cmd_argv(&cmd_ctx, argv, NULL, 0)) {
			if (!(flags & ninja_run_flag_ignore_errors)) {
				LOG_E("%s", cmd_ctx.err_msg);
			}
			goto run_cmd_done;
		}

		if (capture) {
			if (!fs_write(capture, (uint8_t *)cmd_ctx.out.buf, cmd_ctx.out.len)) {
				goto run_cmd_done;
			}
		}

		ret = cmd_ctx.status == 0;
run_cmd_done:
		tstr_destroy(&cmd);
		run_cmd_ctx_destroy(&cmd_ctx);
	}

ret:
	if (argv) {
		z_free((void *)argv);
	}

	if (chdir) {
		path_chdir(cwd.buf);
	}

	tstr_destroy(&cwd);
	return ret;
}
