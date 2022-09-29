#include "posix.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "args.h"
#include "backend/ninja.h"
#include "backend/output.h"
#include "cmd_test.h"
#include "error.h"
#include "functions/environment.h"
#include "lang/serial.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "platform/term.h"

#define MAX_TEST_WORKERS (uint32_t)(sizeof(uint32_t) * 8)

enum test_result_status {
	test_result_status_running,
	test_result_status_ok,
	test_result_status_failed,
	test_result_status_timedout,
};

struct test_result {
	struct run_cmd_ctx cmd_ctx;
	struct obj_test *test;
	struct timespec start;
	float dur, timeout;
	enum test_result_status status;
};

struct run_test_ctx {
	struct test_options *opts;
	obj proj_name;
	obj collected_tests;
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

	struct {
		obj env;
		obj exclude_suites;
		obj wrapper;
		float timeout_multiplier;
	} setup;

	struct darr test_results;

	struct test_result *test_ctx;
	uint32_t test_cmd_ctx_free;
	bool serial;
};

/*
 * Test labeling and output
 */

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
		status_timedout,
	} status = status_ok;

	const char *status_msg[] = {
		[status_failed]             = "fail ",
		[status_should_have_failed] = "ok*  ",
		[status_ok]                 = "ok   ",
		[status_failed_ok]          = "fail*",
		[status_running]            = "start",
		[status_timedout]           = "timeout",
	};

	switch (res->status) {
	case test_result_status_running:
		status = status_running;
		break;
	case test_result_status_timedout:
		status = status_timedout;
		break;
	case test_result_status_failed:
		if (res->test->should_fail) {
			status = status_should_have_failed;
		} else {
			status = status_failed;
		}
		break;
	case test_result_status_ok:
		if (res->test->should_fail) {
			status = status_failed_ok;
		} else {
			status = status_ok;
		}
		break;
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
			[status_timedout] = 31,
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
print_test_progress(struct workspace *wk, struct run_test_ctx *ctx, const struct test_result *res, bool write_line)
{
	if (res->status != test_result_status_running) {
		++ctx->stats.total_count;
		++ctx->stats.test_i;
		if (res->status != test_result_status_ok) {
			++ctx->stats.total_error_count;
			++ctx->stats.error_count;
		}
	}

	if (!ctx->stats.term && !ctx->opts->verbosity) {
		if (res->status != test_result_status_running) {
			char c;
			switch (res->status) {
			case test_result_status_failed:
				c = 'E';
				break;
			case test_result_status_timedout:
				c = 'T';
				break;
			default:
				c = '.';
				break;
			}

			log_plain("%c", c);
		}
		return;
	} else if (ctx->stats.term) {
		log_plain("\r");
	}

	if (write_line && ctx->opts->verbosity > 0) {
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

/*
 * test setup / suites
 */

static bool
project_namespaced_name_matches(const char *name1, bool proj2_is_main,
	const struct str *proj2, const struct str *name2)
{
	struct str proj1 = { 0 };
	const char *sep;
	if ((sep = strchr(name1, ':'))) {
		proj1 = (struct str){ .s = name1, .len = sep - name1 };
		name1 = sep + 1;
	}

	if (proj1.len) {
		if (!str_eql(&proj1, proj2)) {
			return false;
		}
	} else {
		if (!proj2_is_main) {
			return false;
		}
	}

	return str_eql(&WKSTR(name1), name2);
}

struct test_in_suite_ctx {
	struct run_test_ctx *run_test_ctx;
	bool found;

	obj suite;
};

static enum iteration_result
test_in_suite_iter(struct workspace *wk, void *_ctx, obj s)
{
	struct test_in_suite_ctx *ctx = _ctx;
	uint32_t i;

	struct test_options *opts = ctx->run_test_ctx->opts;

	for (i = 0; i < opts->suites_len; ++i) {
		if (!project_namespaced_name_matches(opts->suites[i],
			ctx->run_test_ctx->proj_i == 0,
			get_str(wk, ctx->run_test_ctx->proj_name), get_str(wk, s))) {
			continue;
		}

		ctx->found = true;
		return ir_done;
	}

	return ir_cont;
}

static enum iteration_result
test_in_exclude_suites_exclude_suites_iter(struct workspace *wk, void *_ctx, obj exclude)
{
	struct test_in_suite_ctx *ctx = _ctx;

	if (project_namespaced_name_matches(get_cstr(wk, exclude),
		ctx->run_test_ctx->proj_i == 0,
		get_str(wk, ctx->run_test_ctx->proj_name), get_str(wk, ctx->suite))) {

		ctx->found = true;
		return ir_done;
	}

	return ir_cont;
}

static enum iteration_result
test_in_exclude_suites_iter(struct workspace *wk, void *_ctx, obj suite)
{
	struct test_in_suite_ctx *ctx = _ctx;
	ctx->suite = suite;

	obj_array_foreach(wk, ctx->run_test_ctx->setup.exclude_suites,
		ctx, test_in_exclude_suites_exclude_suites_iter);

	if (ctx->found) {
		return ir_done;
	}
	return ir_cont;
}

static bool
test_in_suite(struct workspace *wk, obj suites, struct run_test_ctx *run_test_ctx)
{
	struct test_in_suite_ctx ctx = {
		.run_test_ctx = run_test_ctx,
	};

	if (!run_test_ctx->opts->suites_len) {
		// no suites given on command line

		if (run_test_ctx->setup.exclude_suites) {
			obj_array_foreach(wk, suites, &ctx, test_in_exclude_suites_iter);
			return !ctx.found;
		} else {
			return true;
		}
	} else if (!suites) {
		// suites given on command line, but test has no suites
		return false;
	}

	obj_array_foreach(wk, suites, &ctx, test_in_suite_iter);
	return ctx.found;
}

struct find_test_setup_ctx {
	struct run_test_ctx *rtctx;
	bool found;
};

static enum iteration_result
find_test_setup_iter(struct workspace *wk, void *_ctx, obj arr)
{
	struct find_test_setup_ctx *ctx = _ctx;

	/* [name, env, exclude_suites, exe_wrapper, is_default, timeout_multiplier] */
	obj name, env, exclude_suites, exe_wrapper, is_default, timeout_multiplier;

	obj_array_index(wk, arr, 0, &name);
	obj_array_index(wk, arr, 1, &env);
	obj_array_index(wk, arr, 2, &exclude_suites);
	obj_array_index(wk, arr, 3, &exe_wrapper);
	obj_array_index(wk, arr, 4, &is_default);
	obj_array_index(wk, arr, 5, &timeout_multiplier);

	if (ctx->rtctx->opts->setup) {
		if (!project_namespaced_name_matches(ctx->rtctx->opts->setup,
			ctx->rtctx->proj_i == 0,
			get_str(wk, ctx->rtctx->proj_name), get_str(wk, name))) {
			return ir_cont;
		}
	} else if (!is_default || !get_obj_bool(wk, is_default)) {
		return ir_cont;
	}

	if (ctx->rtctx->opts->setup) {
		L("using test setup '%s'", ctx->rtctx->opts->setup);
	} else {
		L("using default test setup '%s'", get_cstr(wk, name));
	}

	ctx->rtctx->setup.env = env;
	ctx->rtctx->setup.exclude_suites = exclude_suites;
	ctx->rtctx->setup.wrapper = exe_wrapper;
	ctx->rtctx->setup.timeout_multiplier =
		timeout_multiplier ? get_obj_number(wk, timeout_multiplier)
			: 1.0f;
	ctx->found = true;
	return ir_done;
}

static enum iteration_result
find_test_setup_project_iter(struct workspace *wk, void *_ctx, obj project_name, obj arr)
{
	struct find_test_setup_ctx *ctx = _ctx;
	ctx->rtctx->proj_name = project_name;

	obj setups;
	obj_array_index(wk, arr, 1, &setups);

	if (!setups) {
		return ir_cont;
	}

	obj_array_foreach(wk, setups, ctx, find_test_setup_iter);

	if (ctx->found) {
		return ir_done;
	}

	++ctx->rtctx->proj_i;
	return ir_cont;
}

static bool
load_test_setup(struct workspace *wk, struct run_test_ctx *rtctx, obj tests_dict)
{
	bool res = false;
	struct find_test_setup_ctx ctx = {
		.rtctx = rtctx,
	};

	obj_dict_foreach(wk, tests_dict, &ctx, find_test_setup_project_iter);

	if (!ctx.found) {
		if (rtctx->opts->setup) {
			LOG_E("invalid test setup: '%s'", rtctx->opts->setup);
			goto ret;
		}
	}

	res = true;
ret:
	rtctx->proj_i = 0;
	return res;
}

/*
 * Test runner
 */

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
test_delay(void)
{
	struct timespec req = {
		.tv_nsec = 10000000,
	};
	nanosleep(&req, NULL);
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
		calculate_test_duration(res);

		enum run_cmd_state state = run_cmd_collect(&res->cmd_ctx);

		if (state != run_cmd_running
		    && res->status == test_result_status_timedout) {
			run_cmd_ctx_destroy(&res->cmd_ctx);
			print_test_progress(wk, ctx, res, true);
			darr_push(&ctx->test_results, res);
			goto free_slot;
		}

		switch (state) {
		case run_cmd_running: {
			if (res->timeout > 0.0f && res->dur >= res->timeout) {
				bool force_kill = res->status == test_result_status_timedout
						  && (res->dur - res->timeout) > 0.5f;

				run_cmd_kill(&res->cmd_ctx, force_kill);

				if (!res->status) {
					res->status = test_result_status_timedout;
				}
			}

			continue;
		}
		case run_cmd_error:
			res->status = test_result_status_failed;
			print_test_progress(wk, ctx, res, true);
			darr_push(&ctx->test_results, res);
			break;
		case run_cmd_finished: {
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

			print_test_progress(wk, ctx, res, true);
			darr_push(&ctx->test_results, res);
			break;
		}
		}

free_slot:
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
	ctx->test_ctx[i].timeout =
		(test->timeout ? get_obj_number(wk, test->timeout) : 30.0f)
		* ctx->setup.timeout_multiplier;

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

	print_test_progress(wk, ctx, res, ctx->serial);

	if (!run_cmd(cmd_ctx, argstr, argc, envstr, envc)) {
		ctx->test_cmd_ctx_free &= ~(1 << i);
		calculate_test_duration(res);
		res->status = test_result_status_failed;
		print_test_progress(wk, ctx, res, true);
		darr_push(&ctx->test_results, res);
	}
}

static enum iteration_result
run_test(struct workspace *wk, void *_ctx, obj t)
{
	struct run_test_ctx *ctx = _ctx;

	if (ctx->opts->fail_fast && ctx->stats.total_error_count) {
		return ir_done;
	}

	struct obj_test *test = get_obj_test(wk, t);

	obj cmdline;
	make_obj(wk, &cmdline, obj_array);

	if (ctx->setup.wrapper) {
		obj_array_extend(wk, cmdline, ctx->setup.wrapper);
	}

	obj_array_push(wk, cmdline, test->exe);

	if (test->args) {
		obj_array_extend_nodup(wk, cmdline, test->args);
	}

	const char *argstr, *envstr;
	uint32_t argc, envc;

	obj env;
	if (!environment_to_dict(wk, test->env, &env)) {
		UNREACHABLE;
	}

	if (ctx->setup.env) {
		obj setup_env;
		if (!environment_to_dict(wk, ctx->setup.env, &setup_env)) {
			UNREACHABLE;
		}

		obj merged;
		obj_dict_merge(wk, env, setup_env, &merged);
		env = merged;
	}

	join_args_argstr(wk, &argstr, &argc, cmdline);
	env_to_envstr(wk, &envstr, &envc, env);
	push_test(wk, ctx, test, argstr, argc, envstr, envc);
	return ir_cont;
}

/*
 * Test filtering and dispatch
 */

static enum iteration_result
gather_project_tests_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct run_test_ctx *ctx = _ctx;
	struct obj_test *t = get_obj_test(wk, val);

	if (!(t->category == ctx->opts->cat
	      && test_in_suite(wk, t->suites, ctx))) {
		return ir_cont;
	}

	obj_array_push(wk, ctx->collected_tests, val);

	++ctx->stats.test_len;
	if (t->depends) {
		obj_array_extend_nodup(wk, ctx->deps, t->depends);
	}
	return ir_cont;
}

static enum iteration_result
run_project_tests(struct workspace *wk, void *_ctx, obj proj_name, obj arr)
{
	obj tests;
	obj_array_index(wk, arr, 0, &tests);

	struct run_test_ctx *ctx = _ctx;
	make_obj(wk, &ctx->deps, obj_array);

	ctx->stats.test_i = 0;
	ctx->stats.error_count = 0;
	ctx->stats.test_len = 0;

	make_obj(wk, &ctx->collected_tests, obj_array);
	obj_array_foreach(wk, tests, ctx, gather_project_tests_iter);

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

	if (!obj_array_foreach(wk, ctx->collected_tests, ctx, run_test)) {
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
	SBUF_manual(tests_src);

	if (opts->workers > MAX_TEST_WORKERS) {
		LOG_E("the maximum number of test jobs is %d", MAX_TEST_WORKERS);
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
		.setup = { .timeout_multiplier = 1.0f, },
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
	}

	if (!load_test_setup(&wk, &ctx, tests_dict)) {
		goto ret;
	}

	if (!obj_dict_foreach(&wk, tests_dict, &ctx, run_project_tests)) {
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

		if (opts->print_summary ||
		    (res->status == test_result_status_failed
		     || res->status == test_result_status_timedout)) {
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
		} else if (res->status == test_result_status_timedout) {
			ret = false;
		}
	}

ret:
	workspace_destroy_bare(&wk);
	darr_destroy(&ctx.test_results);
	z_free(ctx.test_ctx);
	return ret;
}
