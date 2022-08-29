#include "posix.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "args.h"
#include "backend/ninja.h"
#include "backend/output.h"
#include "buf_size.h"
#include "cmd_test.h"
#include "error.h"
#include "lang/serial.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "platform/term.h"

#define MAX_TEST_WORKERS (uint32_t)(sizeof(uint32_t) * 8)

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

enum test_result_status {
	test_result_status_running,
	test_result_status_ok,
	test_result_status_failed,
};

struct test_result {
	struct run_cmd_ctx cmd_ctx;
	struct obj_test *test;
	struct timespec start;
	float dur;
	enum test_result_status status;
};

struct run_test_ctx {
	struct test_options *opts;
	obj proj_name;
	obj deps;
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

	struct test_result *test_ctx;
	uint32_t test_cmd_ctx_free;
	bool serial;
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
print_test_result(struct workspace *wk, const struct test_result *res)
{
	const char *name = get_cstr(wk, res->test->name);

	enum {
		status_failed,
		status_should_have_failed,
		status_ok,
		status_failed_ok,
		status_running,
	} status;

	const char *status_msg[] = {
		[status_failed] = "fail ",
		[status_should_have_failed] = "ok*  ",
		[status_ok] = "ok   ",
		[status_failed_ok] = "fail*",
		[status_running] = "start",
	};

	if (res->status == test_result_status_running) {
		status = status_running;
	} else if (res->status == test_result_status_failed) {
		if (res->test->should_fail) {
			status = status_should_have_failed;
		} else {
			status = status_failed;
		}
	} else {
		if (res->test->should_fail) {
			status = status_failed_ok;
		} else {
			status = status_ok;
		}
	}

	const char *suite_str = NULL;
	uint32_t suites_len = 0;
	if (res->test->suites) {
		suites_len = get_obj_array(wk, res->test->suites)->len;
		if (suites_len == 1) {
			obj s;
			obj_array_index(wk, res->test->suites, 0, &s);
			suite_str = get_cstr(wk, s);
		} else if (suites_len > 1) {
			obj s;
			obj_array_join(wk, true, res->test->suites, make_str(wk, ", "), &s);
			suite_str = get_cstr(wk, s);
		}
	}

	if (log_clr()) {
		uint32_t clr[] = {
			[status_failed] = 31,
			[status_should_have_failed] = 31,
			[status_ok] = 32,
			[status_failed_ok] = 33,
			[status_running] = 0,
		};
		log_plain("[\033[%dm%s\033[0m]", clr[status], status_msg[status]);
	} else {
		log_plain("[%s]", status_msg[status]);
	}

	if (res->status == test_result_status_running) {
		log_plain("          ");
	} else {
		log_plain(" %6.2fs, ", res->dur);
	}

	if (suite_str) {
		if (suites_len > 1) {
			log_plain("[%s]:", suite_str);
		} else {
			log_plain("%s:", suite_str);
		}
	}
	log_plain("%s", name);

	if (status == status_should_have_failed) {
		log_plain(" - passing test marked as should_fail");
	}
}

static void
print_test_progress(struct workspace *wk, struct run_test_ctx *ctx, const struct test_result *res)
{
	if (res->status != test_result_status_running) {
		++ctx->stats.total_count;
		++ctx->stats.test_i;
		if (res->status == test_result_status_failed) {
			++ctx->stats.total_error_count;
			++ctx->stats.error_count;
		}
	}

	if (!ctx->stats.term && !ctx->opts->verbosity) {
		if (res->status != test_result_status_running) {
			log_plain("%c", res->status == test_result_status_failed ? 'E' : '.');
		}
		return;
	} else if (ctx->stats.term) {
		log_plain("\r");
	}

	if (ctx->opts->verbosity > 0) {
		print_test_result(wk, res);

		if (ctx->stats.term) {
			log_plain("\033[K");
		}

		log_plain("\n");
	}

	if (!ctx->stats.term) {
		return;
	}

	uint32_t i, running = 0;
	for (i = 0; i < ctx->opts->workers; ++i) {
		if ((ctx->test_cmd_ctx_free & (1 << i))) {
			++running;
		}
	}

	uint32_t pad = 2;

	char info[BUF_SIZE_4k];
	pad += snprintf(info, BUF_SIZE_4k, "%d/%d f: %d (%d) ", ctx->stats.test_i, ctx->stats.test_len, ctx->stats.error_count, running);

	log_plain("%s[", info);
	uint32_t pct = (float)(ctx->stats.test_i) * (float)(ctx->stats.term_width - pad) / (float)ctx->stats.test_len;
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

	for (i = 0; i < ctx->opts->workers; ++i) {
		if (!(ctx->test_cmd_ctx_free & (1 << i))) {
			continue;
		}

		struct test_result *res = &ctx->test_ctx[i];

		switch (run_cmd_collect(&res->cmd_ctx)) {
		case run_cmd_running:
			continue;
		case run_cmd_error:
			calculate_test_duration(res);

			res->status = test_result_status_failed;
			print_test_progress(wk, ctx, res);
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

			if (ok) {
				if (res->test->should_fail) {
					++ctx->stats.total_expect_fail_count;
				}

				res->status = test_result_status_ok;
				run_cmd_ctx_destroy(&res->cmd_ctx);
			} else {
				res->status = test_result_status_failed;
			}

			print_test_progress(wk, ctx, res);
			darr_push(&ctx->test_results, res);
			break;
		}
		}

		ctx->test_cmd_ctx_free &= ~(1 << i);

		if (!res->test->is_parallel) {
			ctx->serial = false;
		}

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
push_test(struct workspace *wk, struct run_test_ctx *ctx, struct obj_test *test,
	const char *argstr, uint32_t argc, const char *envstr, uint32_t envc)
{
	uint32_t i;
	while (true) {
		if (ctx->serial && ctx->test_cmd_ctx_free) {
			goto cont;
		}

		if (test->is_parallel) {
			for (i = 0; i < ctx->opts->workers; ++i) {
				if (!(ctx->test_cmd_ctx_free & (1 << i))) {
					goto found_slot;
				}
			}
		} else {
			if (!ctx->test_cmd_ctx_free) {
				ctx->serial = true;
				i = 0;
				goto found_slot;
			}
		}

cont:
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

	if (ctx->opts->verbosity > 1) {
		cmd_ctx->flags |= run_cmd_ctx_flag_dont_capture;
	}

	if (test->workdir) {
		cmd_ctx->chdir = get_cstr(wk, test->workdir);
	}

	if (clock_gettime(CLOCK_MONOTONIC, &res->start)) {
		LOG_E("error getting test start time: %s", strerror(errno));
	}

	if (ctx->serial) {
		print_test_progress(wk, ctx, res);
	}

	if (!run_cmd(cmd_ctx, argstr, argc, envstr, envc)) {
		ctx->test_cmd_ctx_free &= ~(1 << i);
		calculate_test_duration(res);
		res->status = test_result_status_failed;
		print_test_progress(wk, ctx, res);
		darr_push(&ctx->test_results, res);
	}
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
		obj_array_extend_nodup(wk, cmdline, test->args);
	}

	const char *argstr, *envstr;
	uint32_t argc, envc;

	join_args_argstr(wk, &argstr, &argc, cmdline);
	env_to_envstr(wk, &envstr, &envc, test->env);
	push_test(wk, ctx, test, argstr, argc, envstr, envc);
	return ir_cont;
}

static enum iteration_result
count_project_tests_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct run_test_ctx *ctx = _ctx;
	struct obj_test *t = get_obj_test(wk, val);

	if (!should_run_test(wk, ctx, t)) {
		return ir_cont;
	}

	++ctx->stats.test_len;
	if (t->depends) {
		obj_array_extend_nodup(wk, ctx->deps, t->depends);
	}
	return ir_cont;
}

static enum iteration_result
run_project_tests(struct workspace *wk, void *_ctx, obj proj_name, obj tests)
{
	struct run_test_ctx *ctx = _ctx;
	make_obj(wk, &ctx->deps, obj_array);

	ctx->stats.test_i = 0;
	ctx->stats.error_count = 0;
	ctx->stats.test_len = 0;

	obj_array_foreach(wk, tests, ctx, count_project_tests_iter);

	if (!ctx->stats.test_len) {
		return ir_cont;
	}

	if (get_obj_array(wk, ctx->deps)->len && !ctx->opts->no_rebuild) {
		obj ninja_cmd;
		obj_array_dedup(wk, ctx->deps, &ninja_cmd);
		const char *argstr;
		uint32_t argc;
		join_args_argstr(wk, &argstr, &argc, ninja_cmd);
		if (ninja_run(argstr, argc, NULL, NULL) != 0) {
			LOG_W("failed to run ninja");
		}
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
	SBUF_1k(tests_src, sbuf_flag_overflow_alloc);

	if (opts->workers > MAX_TEST_WORKERS) {
		LOG_E("the maximum number of test workers is %d", MAX_TEST_WORKERS);
		return false;
	} else if (!opts->workers) {
		opts->workers = 4;
	}

	path_join(NULL, &tests_src, output_path.private_dir, output_path.tests);

	FILE *f;
	f = fs_fopen(tests_src.buf, "r");

	sbuf_destroy(&tests_src);

	if (!f) {
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
			ctx.stats.term = true;
			term_winsize(fd, &h, &ctx.stats.term_width);
		} else if (opts->display == test_display_dots) {
			ctx.stats.term = false;
		} else {
			assert(false && "unreachable");
		}
	}

	darr_init(&ctx.test_results, 32, sizeof(struct test_result));
	ctx.test_ctx = z_calloc(ctx.opts->workers, sizeof(struct test_result));

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

		if (opts->print_summary || res->status == test_result_status_failed) {
			print_test_result(&wk, res);
			if (res->status == test_result_status_failed && res->cmd_ctx.err_msg) {
				log_plain(": %s", res->cmd_ctx.err_msg);
			}
			log_plain("\n");
		}

		if (res->status == test_result_status_failed) {
			if (res->test->should_fail) {
				ret = false;
			} else {
				ret = false;
				if (res->cmd_ctx.out.len) {
					log_plain("stdout: '%s'\n", res->cmd_ctx.out.buf);
				}
				if (res->cmd_ctx.err.len) {
					log_plain("stderr: '%s'\n", res->cmd_ctx.err.buf);
				}

			}

			run_cmd_ctx_destroy(&res->cmd_ctx);
		}
	}

ret:
	workspace_destroy_bare(&wk);
	darr_destroy(&ctx.test_results);
	z_free(ctx.test_ctx);
	return ret;
}
