#include "posix.h"

#include <limits.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "lang/serial.h"
#include "log.h"
#include "output/output.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "tests.h"

static enum iteration_result
run_test(struct workspace *wk, void *_ctx, uint32_t t)
{
	struct obj *test = get_obj(wk, t);

	struct run_cmd_ctx cmd_ctx = { 0 };

	uint32_t cmdline;
	make_obj(wk, &cmdline, obj_array);
	obj_array_push(wk, cmdline, test->dat.test.exe);

	uint32_t test_args;
	if (!arr_to_args(wk, test->dat.test.args, &test_args)) {
		return ir_err;
	}

	obj_array_extend(wk, cmdline, test_args);

	char *argv[MAX_ARGS];

	if (!join_args_argv(wk, argv, MAX_ARGS, cmdline)) {
		LOG_E("failed to prepare arguments");
		return ir_err;
	}

	if (!run_cmd(&cmd_ctx, wk_objstr(wk, test->dat.test.exe), argv)) {
		if (cmd_ctx.err_msg) {
			LOG_E("%s", cmd_ctx.err_msg);
		} else {
			LOG_E("%s", strerror(cmd_ctx.err_no));
		}

		return ir_err;
	}

	if (cmd_ctx.status && !test->dat.test.should_fail) {
		LOG_E("%s - failed (%d)", wk_objstr(wk, test->dat.test.name), cmd_ctx.status);
		log_plain("%s", cmd_ctx.err);
		return ir_err;
	} else {
		LOG_I("%s - success (%d)", wk_objstr(wk, test->dat.test.name), cmd_ctx.status);
	}

	return ir_cont;
}

bool
tests_run(const char *build_root)
{
	bool ret = true;
	char tests_src[PATH_MAX], private[PATH_MAX];
	if (!path_join(private, PATH_MAX, build_root, outpath.private_dir)) {
		return false;
	} else if (!path_join(tests_src, PATH_MAX, private, outpath.tests)) {
		return false;
	}

	FILE *f;
	if (!(f = fs_fopen(tests_src, "r"))) {
		return false;
	}

	struct workspace wk;
	workspace_init_bare(&wk);

	uint32_t tests_arr;
	if (!serial_load(&wk, &tests_arr, f)) {
		LOG_E("invalid tests file");
		goto ret;
	} else if (!fs_fclose(f)) {
		goto ret;
	} else if (chdir(build_root) != 0) {
		goto ret;
	} else if (!obj_array_foreach(&wk, tests_arr, NULL, run_test)) {
		goto ret;
	}

	ret = true;
ret:
	workspace_destroy_bare(&wk);
	return ret;
}
