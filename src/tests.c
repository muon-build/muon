#include "posix.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "args.h"
#include "backend/output.h"
#include "buf_size.h"
#include "error.h"
#include "lang/serial.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "platform/term.h"
#include "tests.h"

#define MAX_SIMUL_TEST 4
#if __STDC_VERSION__ >= 201112L
_Static_assert(MAX_SIMUL_TEST <= sizeof(uint32_t) * 8, "error");
#endif

static const char *
test_category_label(enum test_category cat)
{
	switch (cat) {
	case test_category_test:
		return "test";
	case test_category_benchmark:
		return "benchmark";
	default:
		UNREACHABLE_RETURN;
	}
}

struct test_result {
	struct run_cmd_ctx cmd_ctx;
	struct obj_test *test;
	struct timespec start;
	float dur;
	bool failed;
};

struct run_test_ctx {
	struct test_options *opts;
	obj proj_name;
	uint32_t proj_i;
	struct {
		uint32_t test_i, test_len, error_count;
		uint32_t total_count, total_error_count, total_expect_fail_count;
		uint32_t total_skipped;
		uint32_t term_width;
		bool term;
		bool ran_tests;
	} stats;

	struct darr test_results;

	struct test_result test_ctx[MAX_SIMUL_TEST];
	uint32_t test_cmd_ctx_free;
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

			if (!str_eql(get_str(wk, ctx->run_test_ctx->proj_name), &proj_str)) {
				continue;
			}
		} else {
			if (ctx->run_test_ctx->proj_i) {
				continue;
			}
		}

		if (!str_eql(get_str(wk, s), &WKSTR(suite))) {
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

static void
print_test_progress(struct run_test_ctx *ctx, bool success)
{
	++ctx->stats.total_count;
	++ctx->stats.test_i;
	if (!success) {
		++ctx->stats.total_error_count;
		++ctx->stats.error_count;
	}

	if (!ctx->stats.term) {
		log_plain("%c", success ? '.' : 'E');
		return;
	}

	uint32_t pad = 2;

	char info[BUF_SIZE_4k];
	pad += snprintf(info, BUF_SIZE_4k, "%d/%d f: %d ", ctx->stats.test_i, ctx->stats.test_len, ctx->stats.error_count);

	log_plain("\r%s[", info);
	uint32_t i,
		 pct = (float)(ctx->stats.test_i) * (float)(ctx->stats.term_width - pad) / (float)ctx->stats.test_len;
	for (i = 0; i < ctx->stats.term_width - pad; ++i) {
		if (i < pct) {
			log_plain("=");
		} else if (i == pct) {
			log_plain(">");
		} else {
			log_plain(" ");
		}
	}
	log_plain("]");
}

static void
calculate_test_duration(struct test_result *res)
{
	struct timespec end;

	if (clock_gettime(CLOCK_MONOTONIC, &end)) {
		LOG_E("error getting test end time: %s", strerror(errno));
		return;
	}

	double secs = (double)end.tv_sec - (double)res->start.tv_sec;
	double ns = ((secs * 1000000000.0) + end.tv_nsec) - res->start.tv_nsec;
	res->dur = ns / 1000000000.0;
}

static void
collect_tests(struct workspace *wk, struct run_test_ctx *ctx)
{
	uint32_t i;

	for (i = 0; i < MAX_SIMUL_TEST; ++i) {
		if (!(ctx->test_cmd_ctx_free & (1 << i))) {
			continue;
		}

		struct test_result *res = &ctx->test_ctx[i];

		switch (run_cmd_collect(&res->cmd_ctx)) {
		case run_cmd_running:
			continue;
		case run_cmd_error:
			calculate_test_duration(res);

			print_test_progress(ctx, false);
			res->failed = true;
			darr_push(&ctx->test_results, res);
			break;
		case run_cmd_finished: {
			calculate_test_duration(res);

			bool ok;
			if (res->cmd_ctx.status == 0) {
				ok = !res->test->should_fail;
			} else if (res->cmd_ctx.status == 77) {
				++ctx->stats.total_skipped;
				ok = true;
			} else if (res->cmd_ctx.status == 99) {
				ok = false;
			} else {
				ok = res->test->should_fail;
			}

			print_test_progress(ctx, ok);

			if (ok) {
				if (res->test->should_fail) {
					++ctx->stats.total_expect_fail_count;
				}

				run_cmd_ctx_destroy(&res->cmd_ctx);
			} else {
				res->failed = true;
			}

			darr_push(&ctx->test_results, res);
			break;
		}
		}

		ctx->test_cmd_ctx_free &= ~(1 << i);

		if (ctx->opts->fail_fast && ctx->stats.total_error_count) {
			break;
		}
	}
}

static void
test_delay(void)
{
	struct timespec req = {
		.tv_nsec = 10000000,
	};
	nanosleep(&req, NULL);
}

static void
push_test(struct workspace *wk, struct run_test_ctx *ctx, struct obj_test *test, const char *argstr, const char *envstr)
{
	uint32_t i;
	while (true) {
		for (i = 0; i < MAX_SIMUL_TEST; ++i) {
			if (!(ctx->test_cmd_ctx_free & (1 << i))) {
				goto found_slot;
			}
		}

		test_delay();
		collect_tests(wk, ctx);
	}
found_slot:

	ctx->test_cmd_ctx_free |= (1 << i);

	struct test_result *res = &ctx->test_ctx[i];
	*res = (struct test_result) { 0 };

	struct run_cmd_ctx *cmd_ctx = &res->cmd_ctx;
	ctx->test_ctx[i].test = test;
	*cmd_ctx = (struct run_cmd_ctx){ .flags = run_cmd_ctx_flag_async };
	if (test->workdir) {
		cmd_ctx->chdir = get_cstr(wk, test->workdir);
	}

	if (clock_gettime(CLOCK_MONOTONIC, &res->start)) {
		LOG_E("error getting test start time: %s", strerror(errno));
	}

	run_cmd(cmd_ctx, argstr, envstr);
}

static bool
should_run_test(struct workspace *wk, struct run_test_ctx *ctx, struct obj_test *test)
{
	return test->category == ctx->opts->cat
	       && test_in_suite(wk, test->suites, ctx);
}

static enum iteration_result
run_test(struct workspace *wk, void *_ctx, obj t)
{
	struct run_test_ctx *ctx = _ctx;

	if (ctx->opts->fail_fast && ctx->stats.total_error_count) {
		return ir_done;
	}

	struct obj_test *test = get_obj_test(wk, t);

	if (!should_run_test(wk, ctx, test)) {
		return ir_cont;
	}

	obj cmdline;
	make_obj(wk, &cmdline, obj_array);
	obj_array_push(wk, cmdline, test->exe);

	if (test->args) {
		obj test_args;
		if (!arr_to_args(wk,
			arr_to_args_build_target | arr_to_args_custom_target,
			test->args, &test_args)) {
			return ir_err;
		}

		obj_array_extend_nodup(wk, cmdline, test_args);
	}

	const char *argstr, *envstr;

	join_args_argstr(wk, &argstr, cmdline);
	env_to_envstr(wk, &envstr, test->env);
	push_test(wk, ctx, test, argstr, envstr);
	return ir_cont;
}

static enum iteration_result
count_project_tests_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct run_test_ctx *ctx = _ctx;
	struct obj_test *t = get_obj_test(wk, val);

	if (should_run_test(wk, ctx, t)) {
		++ctx->stats.test_len;
	}

	return ir_cont;
}

static enum iteration_result
run_project_tests(struct workspace *wk, void *_ctx, obj proj_name, obj tests)
{
	struct run_test_ctx *ctx = _ctx;
	ctx->stats.test_i = 0;
	ctx->stats.error_count = 0;
	ctx->stats.test_len = 0;

	obj_array_foreach(wk, tests, ctx, count_project_tests_iter);

	if (!ctx->stats.test_len) {
		return ir_cont;
	}

	LOG_I("running %ss for project '%s'", test_category_label(ctx->opts->cat), get_cstr(wk, proj_name));

	ctx->stats.ran_tests = true;

	ctx->proj_name = proj_name;

	if (!obj_array_foreach(wk, tests, ctx, run_test)) {
		return ir_err;
	}

	if (ctx->opts->fail_fast && ctx->stats.total_error_count) {
		return ir_done;
	}

	while (ctx->test_cmd_ctx_free) {
		test_delay();
		collect_tests(wk, ctx);
	}

	log_plain("\n");

	++ctx->proj_i;

	return ir_cont;
}

bool
tests_run(struct test_options *opts)
{
	bool ret = false;
	char tests_src[PATH_MAX];

	if (!path_join(tests_src, PATH_MAX, output_path.private_dir, output_path.tests)) {
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

	{
		int fd;
		if (!fs_fileno(log_file(), &fd)) {
			return false;
		}

		if (opts->display == test_display_auto) {
			opts->display = test_display_dots;
			if (term_isterm(fd)) {
				opts->display = test_display_bar;
			}
		}

		if (opts->display == test_display_bar) {
			uint32_t h;
			ctx.stats.term_width = 80;
			ctx.stats.term = true;
			term_winsize(fd, &h, &ctx.stats.term_width);
		} else if (opts->display == test_display_dots) {
			ctx.stats.term = false;
		} else {
			assert(false && "unreachable");
		}
	}

	darr_init(&ctx.test_results, 32, sizeof(struct test_result));

	obj tests_dict;
	if (!serial_load(&wk, &tests_dict, f)) {
		LOG_E("invalid data file");
		goto ret;
	} else if (!fs_fclose(f)) {
		goto ret;
	} else if (!obj_dict_foreach(&wk, tests_dict, &ctx, run_project_tests)) {
		goto ret;
	}

	if (!ctx.stats.ran_tests) {
		LOG_I("no %ss defined", test_category_label(opts->cat));
	} else {
		LOG_I("finished %d %ss, %d expected fail, %d fail, %d skipped",
			ctx.stats.total_count,
			test_category_label(opts->cat),
			ctx.stats.total_expect_fail_count,
			ctx.stats.total_error_count,
			ctx.stats.total_skipped
			);
	}

	ret = true;
	uint32_t i;
	for (i = 0; i < ctx.test_results.len; ++i) {
		struct test_result *res = darr_get(&ctx.test_results, i);

		if (res->failed) {
			if (res->test->should_fail) {
				ret = false;
				LOG_E("%s was marked as should_fail, but it did not", get_cstr(&wk, res->test->name));
			} else {
				ret = false;
				LOG_E("failed: %s %.2fs", get_cstr(&wk, res->test->name), res->dur);
				if (res->cmd_ctx.err_msg) {
					log_plain("%s\n", res->cmd_ctx.err_msg);
				}
				if (res->cmd_ctx.out.len) {
					log_plain("stdout: '%s'\n", res->cmd_ctx.out.buf);
				}
				if (res->cmd_ctx.err.len) {
					log_plain("stderr: '%s'\n", res->cmd_ctx.err.buf);
				}

			}

			run_cmd_ctx_destroy(&res->cmd_ctx);
		} else if (opts->print_summary) {
			LOG_I("ok: %s %.2fs", get_cstr(&wk, res->test->name), res->dur);
		}
	}

ret:
	workspace_destroy_bare(&wk);
	darr_destroy(&ctx.test_results);
	return ret;
}
