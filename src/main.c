#include "posix.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef BOSON_HAVE_SAMU
#include <samu.h>
#endif

#include "eval.h"
#include "filesystem.h"
#include "log.h"
#include "opts.h"
#include "output.h"
#include "parser.h"
#include "run_cmd.h"
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
	LOG_I(log_misc, "boson v%s-%s", boson_version.version, boson_version.vcs_tag);

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
cmd_setup(uint32_t argc, uint32_t argi, char *const argv[])
{
	char cwd[BUF_LEN + 1] = { 0 },
	     build[PATH_MAX + 1] = { 0 },
	     source[PATH_MAX + 1] = { 0 },
	     argv0[PATH_MAX + 1] = { 0 };

	struct workspace wk;
	workspace_init(&wk);

	struct setup_opts opts = { 0 };
	if (!opts_parse_setup(&wk, &opts, argc, argi, argv)) {
		goto err;
	}

	if (!getcwd(cwd, BUF_LEN)) {
		LOG_W(log_misc, "failed getcwd: '%s'", strerror(errno));
		return false;
	}

	snprintf(source, PATH_MAX, "%s/%s", cwd, "meson.build");
	snprintf(build, PATH_MAX, "%s/%s", cwd, opts.build);

	if (argv[0][0] == '/') {
		wk.argv0 = argv[0];
	} else {
		snprintf(argv0, PATH_MAX, "%s/%s", cwd, argv[0]);
		wk.argv0 = argv0;
	}

	wk.source_root = cwd;
	wk.build_root = build;

	LOG_I(log_misc, "source: %s, build: %s", source, build);

	uint32_t project_id;
	if (!eval_project(&wk, NULL, cwd, build, &project_id)) {
		goto err;
	}

	output_build(&wk, build);

	workspace_destroy(&wk);
	return true;
err:
	workspace_destroy(&wk);
	return false;
}

#ifdef BOSON_HAVE_SAMU
static bool
cmd_samu(uint32_t argc, uint32_t argi, char *const argv[])
{
	return samu_main(argc - argi, (char **)&argv[argi]) == 0;
}
#endif

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
#ifdef BOSON_HAVE_SAMU
		{ "samu", cmd_samu },
#endif
		{ 0 },
	};

	return cmd_run(commands, argc, 0, argv, argv[0]) ? 0 : 1;
}
