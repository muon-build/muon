/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "backend/ninja.h"
#include "backend/output.h"
#include "buf_size.h"
#include "cmd_test.h"
#include "embedded.h"
#include "error.h"
#include "formats/ansi.h"
#include "formats/tap.h"
#include "functions/environment.h"
#include "lang/serial.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/os.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "platform/term.h"
#include "platform/timer.h"
#include "util.h"
#include "vsenv.h"

enum test_result_status {
	test_result_status_running,
	test_result_status_ok,
	test_result_status_failed,
	test_result_status_timedout,
	test_result_status_skipped,
};

struct test_result {
	struct run_cmd_ctx cmd_ctx;
	struct obj_test *test;
	struct timer t;
	float dur, timeout;
	enum test_result_status status;
	bool busy;
	struct {
		bool have;
		uint32_t pass, total;
	} subtests;
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
		uint32_t term_width, term_height;
		uint32_t prev_jobs_displayed;
		bool term;
		bool ran_tests;
	} stats;

	struct {
		obj project_env;
		obj env;
		obj exclude_suites;
		obj wrapper;
		float timeout_multiplier;
	} setup;

	struct arr test_results;
	struct arr jobs_sorted;

	struct test_result *jobs;
	uint32_t busy_jobs;
	bool serial;
};

/*
 * Test labeling and output
 */

static const char *
test_category_label(enum test_category cat)
{
	switch (cat) {
	case test_category_test: return "test";
	case test_category_benchmark: return "benchmark";
	default: UNREACHABLE_RETURN;
	}
}

static const char *
test_suites_label(struct workspace *wk, const struct test_result *res)
{
	const char *suite_str = 0;
	uint32_t suites_len = 0;
	if (res->test->suites) {
		suites_len = get_obj_array(wk, res->test->suites)->len;
		if (suites_len == 1) {
			obj s;
			s = obj_array_index(wk, res->test->suites, 0);
			suite_str = get_cstr(wk, s);
		} else if (suites_len > 1) {
			obj s;
			obj_array_join(wk, true, res->test->suites, make_str(wk, "+"), &s);
			suite_str = get_cstr(wk, s);
		}
	}
	return suite_str;
}

enum test_detailed_status {
	status_ok,
	status_failed,
	status_failed_ok,
	status_running,
	status_timedout,
	status_skipped,
	status_should_have_failed,
};

static enum test_detailed_status
test_detailed_status(const struct test_result *res)
{
	enum test_detailed_status status = status_ok;

	switch (res->status) {
	case test_result_status_running: status = status_running; break;
	case test_result_status_timedout: status = status_timedout; break;
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
	case test_result_status_skipped: status = status_skipped; break;
	}

	return status;
}

static void
print_test_result(struct workspace *wk, const struct test_result *res)
{
	const char *name = get_cstr(wk, res->test->name);

	const char *status_msg[] = {
		[status_failed] = "fail ",
		[status_should_have_failed] = "ok*  ",
		[status_ok] = "ok   ",
		[status_failed_ok] = "fail*",
		[status_running] = "start",
		[status_timedout] = "timeout",
		[status_skipped] = "skip ",
	};

	enum test_detailed_status status = test_detailed_status(res);

	const char *suite_str = test_suites_label(wk, res);

	uint32_t clr[] = {
		[status_failed] = c_red,
		[status_should_have_failed] = c_red,
		[status_ok] = c_green,
		[status_failed_ok] = c_yellow,
		[status_running] = 0,
		[status_timedout] = c_red,
		[status_skipped] = c_yellow,
	};
	log_raw("\033[%dm%s\033[0m", clr[status], status_msg[status]);

	if (res->status == test_result_status_running) {
		log_raw("          ");
	} else {
		log_raw(" %6.2fs ", res->dur);
	}

	if (res->subtests.have) {
		log_raw("%3d/%3d subtests, ", res->subtests.pass, res->subtests.total);
	}

	if (suite_str) {
		log_raw("%s:", suite_str);
	}

	log_raw("%s", name);

	if (status == status_should_have_failed) {
		log_raw(" - passing test marked as should_fail");
	}
}

static int32_t
sort_jobs(const void *_a, const void *_b, void *_ctx)
{
	const uint32_t *a = _a, *b = _b;
	struct run_test_ctx *ctx = _ctx;

	const struct test_result *res_a = &ctx->jobs[*a], *res_b = &ctx->jobs[*b];

	if (res_a->dur > res_b->dur) {
		return -1;
	} else if (res_a->dur < res_b->dur) {
		return 1;
	} else {
		return 0;
	}
}

static void
print_test_progress(struct workspace *wk, struct run_test_ctx *ctx, const struct test_result *res, bool write_line)
{
	if (res && res->status != test_result_status_running) {
		++ctx->stats.total_count;
		++ctx->stats.test_i;
		switch (res->status) {
		case test_result_status_running: UNREACHABLE; break;
		case test_result_status_ok:
		case test_result_status_skipped: break;
		case test_result_status_failed:
		case test_result_status_timedout:
			++ctx->stats.total_error_count;
			++ctx->stats.error_count;
			break;
		}
	}

	if (!ctx->stats.term && !ctx->opts->verbosity) {
		if (res && res->status != test_result_status_running) {
			char c;
			switch (res->status) {
			case test_result_status_failed: c = 'E'; break;
			case test_result_status_timedout: c = 'T'; break;
			default: c = '.'; break;
			}

			log_raw("%c", c);
		}
		return;
	} else if (ctx->stats.term) {
		log_raw("\r");
	}

	if (write_line && (ctx->opts->verbosity > 0 || res->test->verbose)) {
		print_test_result(wk, res);

		if (ctx->stats.term) {
			log_raw("\033[K");
		}

		log_raw("\n");
	}

	if (!ctx->stats.term) {
		return;
	}

	uint32_t i, pad = 2;

	char info[BUF_SIZE_4k];
	pad += snprintf(info,
		BUF_SIZE_4k,
		"%d/%d f:%d s:%d j:%d ",
		ctx->stats.test_i,
		ctx->stats.test_len,
		ctx->stats.error_count,
		ctx->stats.total_skipped,
		ctx->busy_jobs);

	log_raw("%s[", info);
	const float pct_scale = (float)(ctx->stats.term_width - pad) / (float)ctx->stats.test_len;
	uint32_t pct_done = (float)(ctx->stats.test_i) * pct_scale;
	uint32_t pct_working = (float)(ctx->stats.test_i + ctx->busy_jobs) * pct_scale;

	for (i = 0; i < ctx->stats.term_width - pad; ++i) {
		if (i <= pct_done) {
			log_raw("=");
		} else if (i < pct_working) {
			log_raw("-");
		} else if (i == pct_working) {
			log_raw(">");
		} else {
			log_raw(" ");
		}
	}
	log_raw("]");

	log_raw("\n");

	arr_sort(&ctx->jobs_sorted, ctx, sort_jobs);

	const uint32_t term_height = ctx->stats.term_height - 2;
	const uint32_t max_jobs_to_display = MIN(term_height, ctx->opts->jobs);
	uint32_t jobs_displayed = 0;

	for (i = 0; i < ctx->opts->jobs; ++i) {
		res = &ctx->jobs[*(uint32_t *)arr_get(&ctx->jobs_sorted, i)];

		if (!res->busy) {
			continue;
		}

		struct {
			bool have;
			struct tap_parse_result result;
		} tap = { 0 };

		if (res->test->protocol == test_protocol_tap) {
			if (res->cmd_ctx.out.buf) {
				tap_parse(res->cmd_ctx.out.buf, res->cmd_ctx.out.len, &tap.result);
				tap.have = true;
			}
		}

		log_raw("%6.2fs %s", res->dur, get_cstr(wk, res->test->name));

		if (tap.have) {
			log_raw(" (%d/", tap.result.pass + tap.result.fail + tap.result.skip);
			if (tap.result.have_plan) {
				log_raw("%d", tap.result.total);
			} else {
				log_raw("?");
			}

			if (tap.result.fail) {
				log_raw(" f:%d", tap.result.fail);
			}

			if (tap.result.skip) {
				log_raw(" s:%d", tap.result.skip);
			}

			log_raw(")");
		}

		log_raw("\033[K\n");

		++jobs_displayed;
		if (jobs_displayed >= max_jobs_to_display) {
			break;
		}
	}
	for (i = jobs_displayed; i < ctx->stats.prev_jobs_displayed; ++i) {
		log_raw("\033[K\n");
	}

	log_raw("\033[%dA", MAX(jobs_displayed, ctx->stats.prev_jobs_displayed) + 1);

	ctx->stats.prev_jobs_displayed = jobs_displayed;
}

/*
 * test setup / suites
 */

/*
 * Matching rules used for -e (setup) and -s (suite).  name1 is user-provided.
 *
 * If name1 is fully qualified with a project name, it will match the main
 * project only.
 *
 * e.g.
 * name == main_proj:name
 * name != sub_proj:name
 * sub_proj:name == sub_proj:name
 */
static bool
project_namespaced_name_matches(const char *name1, bool proj2_is_main, const struct str *proj2, const struct str *name2)
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

	return str_eql(&STRL(name1), name2);
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
			    get_str(wk, ctx->run_test_ctx->proj_name),
			    get_str(wk, s))) {
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
		    get_str(wk, ctx->run_test_ctx->proj_name),
		    get_str(wk, ctx->suite))) {
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

	obj_array_foreach(wk, ctx->run_test_ctx->setup.exclude_suites, ctx, test_in_exclude_suites_exclude_suites_iter);

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

	name = obj_array_index(wk, arr, 0);
	env = obj_array_index(wk, arr, 1);
	exclude_suites = obj_array_index(wk, arr, 2);
	exe_wrapper = obj_array_index(wk, arr, 3);
	is_default = obj_array_index(wk, arr, 4);
	timeout_multiplier = obj_array_index(wk, arr, 5);

	if (ctx->rtctx->opts->setup) {
		if (!project_namespaced_name_matches(ctx->rtctx->opts->setup,
			    ctx->rtctx->proj_i == 0,
			    get_str(wk, ctx->rtctx->proj_name),
			    get_str(wk, name))) {
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
	ctx->rtctx->setup.timeout_multiplier = timeout_multiplier ? get_obj_number(wk, timeout_multiplier) : 1.0f;
	ctx->found = true;
	return ir_done;
}

static enum iteration_result
find_test_setup_project_iter(struct workspace *wk, void *_ctx, obj project_name, obj arr)
{
	struct find_test_setup_ctx *ctx = _ctx;
	ctx->rtctx->proj_name = project_name;

	obj setups;
	setups = obj_array_index(wk, arr, 1);

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

static enum test_result_status
check_test_result_exitcode(struct workspace *wk, const struct run_test_ctx *ctx, const struct test_result *res)
{
	if (res->cmd_ctx.status == 0) {
		return test_result_status_ok;
	} else if (res->cmd_ctx.status == 77) {
		return test_result_status_skipped;
	} else if (res->cmd_ctx.status == 99) {
		return test_result_status_failed;
	} else {
		return test_result_status_failed;
	}
}

static enum test_result_status
check_test_result_tap(struct workspace *wk, const struct run_test_ctx *ctx, struct test_result *res)
{
	enum test_result_status exitcode_status = check_test_result_exitcode(wk, ctx, res);
	if (exitcode_status != test_result_status_ok) {
		return exitcode_status;
	}

	struct tap_parse_result tap_result = { 0 };
	tap_parse(res->cmd_ctx.out.buf, res->cmd_ctx.out.len, &tap_result);

	res->subtests.have = true;
	res->subtests.pass = tap_result.pass + tap_result.skip;
	res->subtests.total = tap_result.total;

	return tap_result.all_ok && res->status == 0 ? test_result_status_ok : test_result_status_failed;
}

static void
collect_tests(struct workspace *wk, struct run_test_ctx *ctx)
{
	uint32_t i;

	if (ctx->stats.term) {
		print_test_progress(wk, ctx, 0, false);
	}

	for (i = 0; i < ctx->opts->jobs; ++i) {
		struct test_result *res = &ctx->jobs[i];

		if (!res->busy) {
			continue;
		}

		res->dur = timer_read(&res->t);

		enum run_cmd_state state = run_cmd_collect(wk, &res->cmd_ctx);

		if (state != run_cmd_running && res->status == test_result_status_timedout) {
			print_test_progress(wk, ctx, res, true);
			arr_push(wk->a, &ctx->test_results, res);
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
			arr_push(wk->a, &ctx->test_results, res);
			break;
		case run_cmd_finished: {
			enum test_result_status status;

			switch (res->test->protocol) {
			case test_protocol_tap: status = check_test_result_tap(wk, ctx, res); break;
			default: status = check_test_result_exitcode(wk, ctx, res); break;
			}

			if (status == test_result_status_skipped) {
				++ctx->stats.total_skipped;
			}

			if (status == test_result_status_failed && res->test->should_fail) {
				status = test_result_status_ok;
			}

			if (status != test_result_status_failed) {
				if (res->test->should_fail) {
					++ctx->stats.total_expect_fail_count;
				}

				res->status = status;
			} else {
				res->status = test_result_status_failed;
			}

			print_test_progress(wk, ctx, res, true);
			arr_push(wk->a, &ctx->test_results, res);
			break;
		}
		}

free_slot:
		res->busy = false;
		--ctx->busy_jobs;

		if (!res->test->is_parallel) {
			ctx->serial = false;
		}

		*res = (struct test_result){ 0 };

		if (ctx->opts->fail_fast && ctx->stats.total_error_count) {
			break;
		}
	}
}

static void
push_test(struct workspace *wk,
	struct run_test_ctx *ctx,
	struct obj_test *test,
	const char *argstr,
	uint32_t argc,
	const char *envstr,
	uint32_t envc)
{
	uint32_t i;
	while (true) {
		if (ctx->serial && ctx->busy_jobs) {
			goto cont;
		}

		if (test->is_parallel) {
			for (i = 0; i < ctx->opts->jobs; ++i) {
				if (!ctx->jobs[i].busy) {
					goto found_slot;
				}
			}
		} else {
			if (!ctx->busy_jobs) {
				ctx->serial = true;
				i = 0;
				goto found_slot;
			}
		}

cont:
		timer_sleep(SLEEP_TIME);
		collect_tests(wk, ctx);
	}
found_slot:
	++ctx->busy_jobs;

	struct test_result *res = &ctx->jobs[i];
	struct run_cmd_ctx *cmd_ctx = &res->cmd_ctx;

	*res = (struct test_result) {
		.busy = true,
		.test = test,
		.timeout = (test->timeout ? get_obj_number(wk, test->timeout) : 30.0f)
			   * ctx->setup.timeout_multiplier,

		.cmd_ctx = {
			.flags = run_cmd_ctx_flag_async,
		},
	};

	if (ctx->opts->verbosity > 1) {
		if (res->test->protocol == test_protocol_tap) {
			cmd_ctx->flags |= run_cmd_ctx_flag_tee;
		} else {
			cmd_ctx->flags |= run_cmd_ctx_flag_dont_capture;
		}
	}

	if (test->workdir) {
		cmd_ctx->chdir = get_cstr(wk, test->workdir);
	}

	timer_start(&res->t);

	print_test_progress(wk, ctx, res, ctx->serial);

	if (!run_cmd(wk, cmd_ctx, argstr, argc, envstr, envc)) {
		res->busy = false;
		--ctx->busy_jobs;

		res->dur = timer_read(&res->t);
		res->status = test_result_status_failed;
		print_test_progress(wk, ctx, res, true);
		arr_push(wk->a, &ctx->test_results, res);
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
	cmdline = make_obj(wk, obj_array);

	if (ctx->setup.wrapper) {
		obj_array_extend(wk, cmdline, ctx->setup.wrapper);
	}

	obj_array_push(wk, cmdline, test->exe);

	if (test->args) {
		obj_array_extend_nodup(wk, cmdline, test->args);
	}

	const char *argstr, *envstr;
	uint32_t argc, envc;

	environment_extend(wk, test->env, ctx->setup.project_env);

	if (ctx->setup.env) {
		environment_extend(wk, test->env, ctx->setup.env);
	}

	obj env;
	if (!environment_to_dict(wk, test->env, &env)) {
		UNREACHABLE;
	}

	join_args_argstr(wk, &argstr, &argc, cmdline);
	env_to_envstr(wk, &envstr, &envc, env);
	push_test(wk, ctx, test, argstr, argc, envstr, envc);
	return ir_cont;
}

/*
 * Test filtering and dispatch
 */

static bool
test_matches_cmdline_tests(struct workspace *wk, struct run_test_ctx *ctx, struct obj_test *test)
{
	if (!ctx->opts->tests_len) {
		return true;
	}

	uint32_t i;
	for (i = 0; i < ctx->opts->tests_len; ++i) {
		const char *testspec = ctx->opts->tests[i];
		const char *sep = strchr(testspec, ':');

		struct str proj = { 0 }, name = { 0 };

		if (sep) {
			proj.s = testspec;
			proj.len = sep - testspec;
			name.s = sep + 1;
			name.len = strlen(name.s);
		} else {
			name.s = testspec;
			name.len = strlen(name.s);
		}

		if (proj.len && !str_eql(&proj, get_str(wk, ctx->proj_name))) {
			continue;
		}

		if (name.len && !str_eql_glob(&name, get_str(wk, test->name))) {
			continue;
		}

		return true;
	}

	return false;
}

static enum iteration_result
gather_project_tests_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct run_test_ctx *ctx = _ctx;
	struct obj_test *t = get_obj_test(wk, val);

	if (!(t->category == ctx->opts->cat && test_in_suite(wk, t->suites, ctx))) {
		return ir_cont;
	}

	if (!test_matches_cmdline_tests(wk, ctx, t)) {
		return ir_cont;
	}

	obj_array_push(wk, ctx->collected_tests, val);

	++ctx->stats.test_len;
	if (t->depends) {
		obj_array_extend_nodup(wk, ctx->deps, t->depends);
	}
	return ir_cont;
}

static int32_t
test_compare(struct workspace *wk, void *_ctx, obj t1_id, obj t2_id)
{
	struct obj_test *t1 = get_obj_test(wk, t1_id), *t2 = get_obj_test(wk, t2_id);

	int64_t p1 = t1->priority ? get_obj_number(wk, t1->priority) : 0,
		p2 = t2->priority ? get_obj_number(wk, t2->priority) : 0;

	if (p1 > p2) {
		return -1;
	} else if (p1 < p2) {
		return 1;
	} else if (t1->is_parallel && t2->is_parallel) {
		return 0;
	} else if (t1->is_parallel) {
		return 1;
	} else {
		return -1;
	}
}

static enum iteration_result
list_tests_iter(struct workspace *wk, void *_ctx, obj test)
{
	struct run_test_ctx *ctx = _ctx;
	struct obj_test *t = get_obj_test(wk, test);
	obj_printf(wk, "%#o", ctx->proj_name);
	if (t->suites) {
		obj_printf(wk, ":%o", t->suites);
	}
	obj_printf(wk, " - %#o\n", t->name);
	return ir_cont;
}

static enum iteration_result
run_project_tests(struct workspace *wk, void *_ctx, obj proj_name, obj arr)
{
	obj unfiltered_tests, tests;
	unfiltered_tests = obj_array_index(wk, arr, 0);

	struct run_test_ctx *ctx = _ctx;
	ctx->setup.project_env = obj_array_index(wk, arr, 3);

	int64_t project_index = get_obj_number(wk, obj_array_index(wk, arr, 2));
	if (!ctx->opts->include_subprojects && project_index != 0) {
		goto cont;
	}

	ctx->proj_name = proj_name;
	ctx->deps = make_obj(wk, obj_array);

	ctx->stats.test_i = 0;
	ctx->stats.error_count = 0;
	ctx->stats.test_len = 0;

	ctx->collected_tests = make_obj(wk, obj_array);
	obj_array_foreach(wk, unfiltered_tests, ctx, gather_project_tests_iter);
	obj_array_sort(wk, NULL, ctx->collected_tests, test_compare, &tests);

	if (ctx->opts->list) {
		obj_array_foreach(wk, tests, ctx, list_tests_iter);
		goto cont;
	} else if (!ctx->stats.test_len) {
		goto cont;
	}

	if (get_obj_array(wk, ctx->deps)->len && !ctx->opts->no_rebuild) {
		obj ninja_cmd;
		obj_array_dedup(wk, ctx->deps, &ninja_cmd);
		if (!ninja_run(wk, ninja_cmd, NULL, NULL, 0)) {
			LOG_E("failed to run ninja");
			return ir_err;
		}
	}

	LOG_I("running %ss for project '%s'", test_category_label(ctx->opts->cat), get_cstr(wk, proj_name));

	ctx->stats.ran_tests = true;

	if (!obj_array_foreach(wk, tests, ctx, run_test)) {
		return ir_err;
	}

	if (ctx->opts->fail_fast && ctx->stats.total_error_count) {
		return ir_done;
	}

	while (ctx->busy_jobs) {
		timer_sleep(SLEEP_TIME);
		collect_tests(wk, ctx);
	}

	log_raw("\n");

cont:
	++ctx->proj_i;
	return ir_cont;
}

static bool
tests_output_term(struct workspace *wk, struct run_test_ctx *ctx)
{
	bool ret = true;

	uint32_t i;
	for (i = 0; i < ctx->test_results.len; ++i) {
		struct test_result *res = arr_get(&ctx->test_results, i);

		if (ctx->opts->print_summary
			|| (res->status == test_result_status_failed || res->status == test_result_status_timedout)) {
			print_test_result(wk, res);
			if (res->status == test_result_status_failed && res->cmd_ctx.err_msg) {
				log_raw(": %s", res->cmd_ctx.err_msg);
			}
			log_raw("\n");
		}

		if (res->status == test_result_status_failed) {
			if (res->test->should_fail) {
				ret = false;
			} else {
				ret = false;
				if (res->cmd_ctx.out.len) {
					log_raw("stdout: '%s'\n", res->cmd_ctx.out.buf);
				}
				if (res->cmd_ctx.err.len) {
					log_raw("stderr: '%s'\n", res->cmd_ctx.err.buf);
				}
			}
		} else if (res->status == test_result_status_timedout) {
			ret = false;
		}
	}

	return ret;
}

static void
tests_as_json(struct workspace *wk, struct run_test_ctx *ctx, struct tstr *data)
{
	const char *status_str[] = {
		[status_failed] = "failed",
		[status_should_have_failed] = "should_have_failed",
		[status_ok] = "ok",
		[status_failed_ok] = "failed_ok",
		[status_running] = "running",
		[status_timedout] = "timedout",
		[status_skipped] = "skipped",
	};

	tstr_push(wk, data, '{');
	tstr_pushf(wk, data, "\"project\":{\"name\":\"%s\"},", get_cstr(wk, ctx->proj_name));
	tstr_pushf(wk, data, "\"tests\":[");

	uint32_t i;
	for (i = 0; i < ctx->test_results.len; ++i) {
		struct test_result *res = arr_get(&ctx->test_results, i);
		enum test_detailed_status status = test_detailed_status(res);
		const char *suite_str = test_suites_label(wk, res);

		tstr_pushf(wk,
			data,
			"{"
			"\"status\":\"%s\","
			"\"name\":\"%s\","
			"\"suite\":\"%s\","
			"\"duration\":%f,",
			status_str[status],
			get_cstr(wk, res->test->name),
			suite_str,
			res->dur);

		if (res->subtests.have) {
			tstr_pushf(wk,
				data,
				"\"subtests\":{\"pass\":%d,\"total\":%d},",
				res->subtests.pass,
				res->subtests.total);
		}

		tstr_pushs(wk, data, "\"stdout\":\"");
		tstr_push_json_escaped(wk, data, res->cmd_ctx.out.buf, res->cmd_ctx.out.len);
		tstr_pushs(wk, data, "\",\"stderr\":\"");
		tstr_push_json_escaped(wk, data, res->cmd_ctx.err.buf, res->cmd_ctx.err.len);
		tstr_pushs(wk, data, "\"");

		tstr_pushs(wk, data, "}");

		if (i + 1 != ctx->test_results.len) {
			tstr_push(wk, data, ',');
		}
	}
	tstr_pushf(wk, data, "]}");
}

static bool
tests_output_html(struct workspace *wk, struct run_test_ctx *ctx)
{
	TSTR(data);

	tests_as_json(wk, ctx, &data);

	TSTR(html_path);
	path_join(wk, &html_path, output_path.private_dir, "tests.html");
	TSTR(abs);
	path_make_absolute(wk, &abs, html_path.buf);

	FILE *f = 0;
	if (!(f = fs_fopen(abs.buf, "wb"))) {
		return false;
	}

	struct source src;
	if (!embedded_get(wk, "html/test_out.html", &src)) {
		UNREACHABLE;
	}

	fprintf(f, src.src, data.buf);
	LOG_I("wrote html output to %s", abs.buf);
	fclose(f);

	return true;
}

static bool
tests_output_json(struct workspace *wk, struct run_test_ctx *ctx)
{
	TSTR(data);

	tests_as_json(wk, ctx, &data);

	TSTR(json_path);
	path_join(wk, &json_path, output_path.private_dir, "tests.json");
	TSTR(abs);
	path_make_absolute(wk, &abs, json_path.buf);

	FILE *f = 0;
	if (!(f = fs_fopen(abs.buf, "wb"))) {
		return false;
	}

	fprintf(f, "%s", data.buf);
	LOG_I("wrote json output to %s", abs.buf);
	fclose(f);

	return true;
}

bool
tests_run(struct workspace *wk, struct test_options *opts, const char *argv0)
{
	bool ret = false;

	wk->vm.lang_mode = language_internal;
	wk->argv0 = argv0;

	setup_platform_env(wk, ".", requirement_required);

	if (!opts->jobs) {
		opts->jobs = os_parallel_job_count();
	}

	struct run_test_ctx ctx = {
		.opts = opts,
		.setup = { .timeout_multiplier = 1.0f, },
	};

	arr_init(wk->a, &ctx.test_results, 32, struct test_result);
	arr_init(wk->a, &ctx.jobs_sorted, ctx.opts->jobs, uint32_t);
	for (uint32_t i = 0; i < ctx.opts->jobs; ++i) {
		arr_push(wk->a, &ctx.jobs_sorted, &i);
	}
	ctx.jobs = z_calloc(ctx.opts->jobs, sizeof(struct test_result));

	{ // load global opts
		obj option_info;
		if (!serial_load_from_private_dir(wk, &option_info, output_path.paths[output_path_option_info].path)) {
			goto ret;
		}
		wk->global_opts = obj_array_index(wk, option_info, 0);
	}

	if (!opts->no_rebuild) {
		obj ninja_cmd;
		ninja_cmd = make_obj(wk, obj_array);
		obj_array_push(wk, ninja_cmd, make_str(wk, "build.ninja"));
		if (!ninja_run(wk, ninja_cmd, NULL, NULL, 0)) {
			return false;
		}
	}

	{
		int term_fd;
		if (!fs_fileno(_log_file(), &term_fd)) {
			return false;
		}

		if (opts->display == test_display_auto) {
			opts->display = test_display_dots;
			if (fs_is_a_tty_from_fd(wk, term_fd)) {
				opts->display = test_display_bar;
			}
		}

		if (opts->display == test_display_bar) {
			ctx.stats.term = true;
			term_winsize(wk, term_fd, &ctx.stats.term_height, &ctx.stats.term_width);
		} else if (opts->display == test_display_dots) {
			ctx.stats.term = false;
		} else {
			assert(false && "unreachable");
		}
	}

	obj tests_dict;
	if (!serial_load_from_private_dir(wk, &tests_dict, output_path.paths[output_path_tests].path)) {
		goto ret;
	}

	if (!load_test_setup(wk, &ctx, tests_dict)) {
		goto ret;
	}

	if (!obj_dict_foreach(wk, tests_dict, &ctx, run_project_tests)) {
		goto ret;
	}

	if (opts->list) {
		ret = true;
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
			ctx.stats.total_skipped);
	}

	switch (opts->output) {
	case test_output_term: ret = tests_output_term(wk, &ctx); break;
	case test_output_html: ret = tests_output_html(wk, &ctx); break;
	case test_output_json: ret = tests_output_json(wk, &ctx); break;
	}

	{
		uint32_t i;
		for (i = 0; i < ctx.test_results.len; ++i) {
			struct test_result *res = arr_get(&ctx.test_results, i);
			if (res->status == test_result_status_failed) {
				if (res->test->should_fail) {
					ret = false;
				} else {
					ret = false;
				}
			} else if (res->status == test_result_status_timedout) {
				ret = false;
			}
			run_cmd_ctx_destroy(&res->cmd_ctx);
		}
	}

ret:
	z_free(ctx.jobs);
	return ret;
}
