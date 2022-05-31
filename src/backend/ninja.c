#include "posix.h"

#include "backend/ninja.h"
#include "backend/ninja/alias_target.h"
#include "backend/ninja/build_target.h"
#include "backend/ninja/custom_target.h"
#include "backend/ninja/rules.h"
#include "backend/output.h"
#include "external/samurai.h"
#include "lang/serial.h"
#include "log.h"
#include "options.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

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
	case obj_both_libs:
		break;
	default:
		LOG_E("invalid tgt type '%s'", obj_type_to_s(t));
		return ir_err;
	}

	return ir_cont;
}

static enum iteration_result
write_tgt_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct write_tgt_ctx *ctx = _ctx;

	enum obj_type t = get_obj_type(wk, tgt_id);
	switch (t) {
	case obj_alias_target:
		return ninja_write_alias_tgt(wk, tgt_id, ctx);
	case obj_both_libs:
		tgt_id = get_obj_both_libs(wk, tgt_id)->dynamic_lib;
	/* fallthrough */
	case obj_build_target:
		return ninja_write_build_tgt(wk, tgt_id, ctx);
	case obj_custom_target:
		return ninja_write_custom_tgt(wk, tgt_id, ctx);
	default:
		LOG_E("invalid tgt type '%s'", obj_type_to_s(t));
		return ir_err;
	}
}

static bool
ninja_write_build(struct workspace *wk, void *_ctx, FILE *out)
{
	struct check_tgt_ctx check_ctx = { 0 };

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = darr_get(&wk->projects, i);

		obj_array_foreach(wk, proj->targets, &check_ctx, check_tgt_iter);
	}

	if (!ninja_write_rules(out, wk, darr_get(&wk->projects, 0), check_ctx.need_phony)) {
		return false;
	}

	bool wrote_default = false;

	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = darr_get(&wk->projects, i);

		struct write_tgt_ctx ctx = { .out = out, .proj = proj };

		if (!obj_array_foreach(wk, proj->targets, &ctx, write_tgt_iter)) {
			return false;
		}

		wrote_default |= ctx.wrote_default;
	}

	if (!wrote_default) {
		fprintf(out,
			"build muon_do_nothing: phony\n"
			"default muon_do_nothing\n"
			);
	}

	return true;
}

static bool
ninja_write_tests(struct workspace *wk, void *_ctx, FILE *out)
{
	bool wrote_header = false;

	obj tests;
	make_obj(wk, &tests, obj_dict);

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = darr_get(&wk->projects, i);

		if (proj->tests && get_obj_array(wk, proj->tests)->len) {
			if (!wrote_header) {
				LOG_I("writing tests");
				wrote_header = true;
			}

			obj res, key;
			key = proj->cfg.name;

			if (obj_dict_index(wk, tests, key, &res)) {
				assert("false" && "project defined multiple times");
				return false;
			}

			obj_dict_set(wk, tests, key, proj->tests);
		}
	}

	return serial_dump(wk, tests, out);
}

static bool
ninja_write_install(struct workspace *wk, void *_ctx, FILE *out)
{
	obj o;
	make_obj(wk, &o, obj_array);
	obj_array_push(wk, o, wk->install);
	obj_array_push(wk, o, wk->install_scripts);
	obj_array_push(wk, o, make_str(wk, wk->source_root));

	struct project *proj = darr_get(&wk->projects, 0);
	obj prefix;
	get_option_value(wk, proj, "prefix", &prefix);
	obj_array_push(wk, o, prefix);

	return serial_dump(wk, o, out);
}

bool
ninja_write_all(struct workspace *wk)
{
	if (!(with_open(wk->build_root, "build.ninja", wk, NULL, ninja_write_build)
	      && with_open(wk->muon_private, output_path.tests, wk, NULL, ninja_write_tests)
	      && with_open(wk->muon_private, output_path.install, wk, NULL, ninja_write_install)
	      )) {
		return false;
	}

	/* compile_commands.json */
	if (have_samurai) {
		char compile_commands[PATH_MAX];
		if (!path_join(compile_commands, PATH_MAX, wk->build_root, "compile_commands.json")) {
			return false;
		}

		if (!muon_samu_compdb(wk->build_root, compile_commands)) {
			return false;
		}
	}

	return true;
}

int
ninja_run(const char *argstr)
{
	char *const *argv;
	uint32_t argc;

	if (have_samurai) {
		argc = argstr_to_argv(argstr, "samu", &argv);
		bool res = muon_samu(argc, argv);
		z_free((void *)argv);
		return res ? 0 : 1;
	}

	const char *cmd = NULL;
	if (!(fs_find_cmd("samu", &cmd)
	      || fs_find_cmd("ninja", &cmd))) {
		LOG_E("unable to find a ninja implementation");
		return 1;
	}

	argc = argstr_to_argv(argstr, cmd, &argv);

	struct run_cmd_ctx cmd_ctx = { .flags = run_cmd_ctx_flag_dont_capture };
	if (!run_cmd_argv(&cmd_ctx, cmd, argv, NULL)) {
		LOG_E("%s", cmd_ctx.err_msg);

		run_cmd_ctx_destroy(&cmd_ctx);
		z_free((void *)argv);

		return 1;
	}

	run_cmd_ctx_destroy(&cmd_ctx);
	z_free((void *)argv);

	return cmd_ctx.status;
}
