#include "posix.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "external/samu.h"
#include "filesystem.h"
#include "inih.h"
#include "log.h"
#include "mem.h"
#include "opts.h"
#include "output.h"
#include "parser.h"
#include "run_cmd.h"
#include "tests.h"
#include "version.h"

#define BUF_LEN 256

typedef bool (*cmd_func)(uint32_t argc, uint32_t argi, char *const[]);
struct command {
	const char *name;
	cmd_func cmd;
};

static void
print_usage(const struct command *commands, const char *pre)
{
	uint32_t i;
	LOG_I(log_misc, "muon v%s-%s", muon_version.version, muon_version.vcs_tag);

	LOG_I(log_misc, "usage: %s COMMAND", pre);

	for (i = 0; commands[i].name; ++i) {
		LOG_I(log_misc, "  %s", commands[i].name);
	}
}

static bool
cmd_run(const struct command *commands, uint32_t argc, uint32_t argi, char *const argv[], const char *pre)
{
	uint32_t i;

	if (argi + 1 >= argc) {
		LOG_W(log_misc, "missing command");
		print_usage(commands, pre);
		return false;
	}

	const char *cmd = argv[argi + 1];

	for (i = 0; commands[i].name; ++i) {
		if (strcmp(commands[i].name, cmd) == 0) {
			return commands[i].cmd(argc, argi + 1, argv);
		}
	}

	LOG_W(log_misc, "unknown command '%s'", argv[argi + 1]);
	print_usage(commands, pre);
	return false;
}

static bool
cmd_internal_exe(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct exe_opts opts = { 0 };
	if (!opts_parse_exe(&opts, argc, argi, argv)) {
		return false;
	}

	struct run_cmd_ctx ctx = { 0 };
	if (!run_cmd(&ctx, opts.cmd[0], opts.cmd)) {
		return false;
	}

	if (ctx.status != 0) {
		fputs(ctx.err, stderr);
		return false;
	}

	if (opts.capture) {
		return fs_write(opts.capture, (uint8_t *)ctx.out, strlen(ctx.out));
	} else {
		fputs(ctx.out, stdout);
		return true;
	}
}

static bool
cmd_internal(uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "exe", cmd_internal_exe },
		0,
	};

	return cmd_run(commands, argc, argi, argv, argv[argi]);
}

static bool
cmd_samu(uint32_t argc, uint32_t argi, char *const argv[])
{
	return muon_samu(argc - argi, (char **)&argv[argi]) == 0;
}

static bool
cmd_parse_check(uint32_t argc, uint32_t argi, char *const argv[])
{
	if (argi + 1 >= argc) {
		LOG_W(log_misc, "missing filename");
		return false;
	}

	struct tokens toks = { 0 };
	struct ast ast = { 0 };
	if (!lexer_lex(language_internal, &toks, argv[argi + 1])) {
		return false;
	} else if (!parser_parse(&ast, &toks)) {
		return false;
	}

	return true;
}

static bool
cmd_ast(uint32_t argc, uint32_t argi, char *const argv[])
{
	if (argi + 1 >= argc) {
		LOG_W(log_misc, "missing filename");
		return false;
	}

	struct tokens toks = { 0 };
	struct ast ast = { 0 };
	if (!lexer_lex(language_internal, &toks, argv[argi + 1])) {
		return false;
	} else if (!parser_parse(&ast, &toks)) {
		return false;
	}

	print_ast(&ast);
	return true;
}

static bool
cmd_eval(uint32_t argc, uint32_t argi, char *const argv[])
{
	if (argi + 1 >= argc) {
		LOG_W(log_misc, "missing filename");
		return false;
	}

	char cwd[PATH_MAX + 1] = { 0 };
	if (!getcwd(cwd, PATH_MAX )) {
		LOG_W(log_misc, "failed getcwd: '%s'", strerror(errno));
		return false;
	}

	struct workspace wk;
	workspace_init(&wk);
	wk.lang_mode = language_internal;
	make_project(&wk, &wk.cur_project, NULL, cwd, "<build_dir>");
	bool ret;
	ret = eval(&wk, argv[argi + 1]);

	workspace_destroy(&wk); // just to test for memory leaks in valgrind
	return ret;
}

static bool
cmd_test(uint32_t argc, uint32_t argi, char *const argv[])
{
	if (argi + 1 >= argc) {
		LOG_W(log_misc, "missing argument: build_dir");
		return false;
	}

	return tests_run(argv[argi + 1]);
}

static bool
setup_workspace_dirs(struct workspace *wk, const char *build, const char *argv0)
{
	static char cwd[BUF_LEN + 1] = { 0 },
		    abs_build[PATH_MAX + 1] = { 0 },
		    abs_argv0[PATH_MAX + 1] = { 0 };

	if (!getcwd(cwd, BUF_LEN)) {
		LOG_W(log_misc, "failed getcwd: '%s'", strerror(errno));
		return false;
	}

	snprintf(abs_build, PATH_MAX, "%s/%s", cwd, build);

	if (argv0[0] == '/') {
		strncpy(abs_argv0, argv0, PATH_MAX);
	} else {
		snprintf(abs_argv0, PATH_MAX, "%s/%s", cwd, argv0);
	}

	wk->argv0 = abs_argv0;
	wk->source_root = cwd;
	wk->build_root = abs_build;
	return true;
}

static bool
do_build(struct workspace *wk)
{
	uint32_t project_id;
	bool ret = false;
	char buf[PATH_MAX + 1] = { 0 };

	snprintf(buf, PATH_MAX, "%s/build.ninja", wk->build_root);

	if (!fs_file_exists(buf)) {
		if (!eval_project(wk, NULL, wk->source_root, wk->build_root, &project_id)) {
			goto ret;
		} else if (!output_build(wk)) {
			goto ret;
		}
	}

	if (chdir(wk->build_root) < 0) {
		goto ret;
	} else if (!muon_samu(0, (char *[]){ "<muon_samu>", NULL })) {
		goto ret;
	} else if (chdir(wk->source_root) < 0) {
		goto ret;
	}

	ret = true;
ret:
	workspace_destroy(wk);
	return ret;
}

static bool
cmd_setup(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace wk;
	workspace_init(&wk);

	struct setup_opts opts = { 0 };
	if (!opts_parse_setup(&wk, &opts, argc, &argi, argv)) {
		goto err;
	}

	if (argi >= argc) {
		LOG_W(log_misc, "missing build directory");
		return false;
	}

	if (!setup_workspace_dirs(&wk, argv[argi], argv[0])) {
		goto err;
	}

	uint32_t project_id;
	if (!eval_project(&wk, NULL, wk.source_root, wk.build_root, &project_id)) {
		goto err;
	}

	if (!output_build(&wk)) {
		goto err;
	}

	workspace_destroy(&wk);
	return true;
err:
	workspace_destroy(&wk);
	return false;
}


struct build_cfg {
	const char *dir, *argv0;
	struct workspace wk;
	bool workspace_init;
};

static bool
build_cfg_cb(void *_cfg, const char *path, const char *sect,
	const char *k, const char *v, uint32_t line)
{
	struct build_cfg *cfg = _cfg;

	if (!sect) {
		error_messagef(path, line, 1, "missing [build_dir]");
		return false;
	}

	if (cfg->dir != sect) {
		if (cfg->workspace_init) {
			if (!do_build(&cfg->wk)) {
				return false;
			}
		}

		workspace_init(&cfg->wk);
		cfg->workspace_init = true;
		cfg->dir = sect;
		if (!setup_workspace_dirs(&cfg->wk, cfg->dir, cfg->argv0)) {
			return false;
		}
	}

	if (!parse_config_key_value(&cfg->wk, (char *)k, v)) {
		return false;
	}
	return true;
}

static bool
cmd_build(uint32_t argc, uint32_t argi, char *const argv[])
{
	const char *build_cfg_src = "muon.ini";
	struct build_cfg cfg = { .argv0 = argv[0] };
	bool ret = false;

	char *ini_buf;
	if (!ini_parse(build_cfg_src, &ini_buf, build_cfg_cb, &cfg)) {
		goto ret;
	}

	if (cfg.workspace_init) {
		if (!do_build(&cfg.wk)) {
			goto ret;
		}
	}

	ret = true;
ret:
	z_free(ini_buf);
	return ret;
}

int
main(int argc, char *argv[])
{
	log_init();
	log_set_lvl(log_debug);
	log_set_filters(0xffffffff & (~log_filter_to_bit(log_mem)));

	assert(argc > 0);

	static const struct command commands[] = {
		{ "internal", cmd_internal },
		{ "setup", cmd_setup },
		{ "eval", cmd_eval },
		{ "ast", cmd_ast },
		{ "parse_check", cmd_parse_check },
		{ "test", cmd_test },
		{ "samu", cmd_samu },
		{ 0 },
	};


	if (argc == 1) {
		return cmd_build(argc, 0, argv) ? 0 : 1;
	}

	return cmd_run(commands, argc, 0, argv, argv[0]) ? 0 : 1;
}
