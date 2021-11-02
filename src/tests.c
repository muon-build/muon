#include "posix.h"

#include <limits.h>
#include <string.h>

#include "args.h"
#include "backend/output.h"
#include "buf_size.h"
#include "lang/serial.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "tests.h"

struct run_test_ctx {
	struct test_options *opts;
	obj proj_name;
	uint32_t proj_i;
};

struct test_in_suite_ctx {
	struct run_test_ctx *run_test_ctx;
	bool found;
};

static enum iteration_result
test_in_suite_iter(struct workspace *wk, void *_ctx, obj s)
{
	struct test_in_suite_ctx *ctx = _ctx;
	uint32_t i;
	const char *sep, *proj, *suite;

	struct test_options *opts = ctx->run_test_ctx->opts;

	for (i = 0; i < opts->suites_len; ++i) {
		if ((sep = strchr(opts->suites[i], ':'))) {
			proj = opts->suites[i];
			suite = sep + 1;
		} else {
			proj = NULL;
			suite = opts->suites[i];
		}


		if (proj) {
			struct str proj_str = {
				.s = proj,
				.len = sep - proj,
			};

			if (!wk_streql(get_str(wk, ctx->run_test_ctx->proj_name), &proj_str)) {
				continue;
			}
		} else {
			if (ctx->run_test_ctx->proj_i) {
				continue;
			}
		}

		if (!wk_streql(get_str(wk, s), &WKSTR(suite))) {
			continue;
		}

		ctx->found = true;
		return ir_done;
	}

	return ir_cont;
}

static bool
test_in_suite(struct workspace *wk, obj suites, struct run_test_ctx *run_test_ctx)
{
	if (!run_test_ctx->opts->suites_len) {
		// no suites given on command line
		return true;
	} else if (!suites) {
		// suites given on command line, but test has no suites
		return false;
	}

	struct test_in_suite_ctx ctx = {
		.run_test_ctx = run_test_ctx,
	};

	if (!obj_array_foreach(wk, suites, &ctx, test_in_suite_iter)) {
		return false;
	}

	return ctx.found;
}

static enum iteration_result
run_test(struct workspace *wk, void *_ctx, uint32_t t)
{
	struct run_test_ctx *ctx = _ctx;
	struct obj *test = get_obj(wk, t);

	if (!test_in_suite(wk, test->dat.test.suites, ctx)) {
		return ir_cont;
	}

	uint32_t cmdline;
	make_obj(wk, &cmdline, obj_array);
	obj_array_push(wk, cmdline, test->dat.test.exe);

	if (test->dat.test.args) {
		uint32_t test_args;
		if (!arr_to_args(wk, test->dat.test.args, &test_args)) {
			return ir_err;
		}

		obj_array_extend(wk, cmdline, test_args);
	}

	const char *argv[MAX_ARGS];
	char *const *envp = NULL;

	if (!join_args_argv(wk, argv, MAX_ARGS, cmdline)) {
		LOG_E("failed to prepare arguments");
		return ir_err;
	}

	if (!env_to_envp(wk, 0, &envp, test->dat.test.env, 0)) {
		LOG_E("failed to prepare environment");
		return ir_err;
	}

	enum iteration_result ret = ir_err;
	struct run_cmd_ctx cmd_ctx = { 0 };

	if (!run_cmd(&cmd_ctx, get_cstr(wk, test->dat.test.exe), argv, envp)) {
		LOG_E("test command failed: %s", cmd_ctx.err_msg);
		goto ret;
	}

	if (cmd_ctx.status && !test->dat.test.should_fail) {
		LOG_E("%s - failed (%d)", get_cstr(wk, test->dat.test.name), cmd_ctx.status);
		LOG_E("stdout: '%s'", cmd_ctx.out);
		LOG_E("stderr: '%s'", cmd_ctx.err);
		goto ret;
	} else {
		LOG_I("%s - success (%d)", get_cstr(wk, test->dat.test.name), cmd_ctx.status);
	}

	ret = ir_cont;
ret:
	run_cmd_ctx_destroy(&cmd_ctx);
	return ret;
}

static enum iteration_result
run_project_tests(struct workspace *wk, void *_ctx, obj proj_name, obj tests)
{
	if (!get_obj(wk, tests)->dat.arr.len) {
		return ir_cont;
	}

	LOG_I("running tests for project '%s'", get_cstr(wk, proj_name));

	struct run_test_ctx *ctx = _ctx;

	ctx->proj_name = proj_name;

	if (!obj_array_foreach(wk, tests, ctx, run_test)) {
		return ir_err;
	}

	++ctx->proj_i;

	return ir_cont;
}

bool
tests_run(const char *build_dir, struct test_options *opts)
{
	bool ret = false;
	char tests_src[PATH_MAX], private[PATH_MAX], build_root[PATH_MAX];

	if (!path_make_absolute(build_root, PATH_MAX, build_dir)) {
		return false;
	} else if (!path_join(private, PATH_MAX, build_root, output_path.private_dir)) {
		return false;
	} else if (!path_join(tests_src, PATH_MAX, private, output_path.tests)) {
		return false;
	}

	FILE *f;
	if (!(f = fs_fopen(tests_src, "r"))) {
		return false;
	}

	struct workspace wk;
	workspace_init_bare(&wk);

	struct run_test_ctx ctx = {
		.opts = opts,
	};

	uint32_t tests_dict;
	if (!serial_load(&wk, &tests_dict, f)) {
		LOG_E("invalid tests file");
		goto ret;
	} else if (!fs_fclose(f)) {
		goto ret;
	} else if (!path_chdir(build_root)) {
		goto ret;
	} else if (!obj_dict_foreach(&wk, tests_dict, &ctx, run_project_tests)) {
		goto ret;
	}

	ret = true;
ret:
	workspace_destroy_bare(&wk);
	return ret;
}
