#include "posix.h"

#include <errno.h>
#include <limits.h>
#include <samu.h>
#include <string.h>

#include "eval.h"
#include "filesystem.h"
#include "log.h"
#include "output.h"
#include "parser.h"

#define BUF_LEN 256

static bool
cmd_setup(int argc, const char *argv[])
{
	if (argc < 2) {
		LOG_W(log_misc, "missing build directory");
		return false;
	}

	char cwd[BUF_LEN + 1] = { 0 },
	     build[PATH_MAX + 1] = { 0 },
	     source[PATH_MAX + 1] = { 0 };

	if (!getcwd(cwd, BUF_LEN)) {
		LOG_W(log_misc, "failed getcwd: '%s'", strerror(errno));
		return false;
	}

	snprintf(source, PATH_MAX, "%s/%s", cwd, "meson.build");
	snprintf(build, PATH_MAX, "%s/%s", cwd, argv[1]);

	LOG_I(log_misc, "source: %s, build: %s", source, build);

	struct workspace wk;
	workspace_init(&wk);
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

static bool
cmd_samu(int argc, const char *argv[])
{
	return samu_main(argc, (char **)argv) == 0;
}

static bool
cmd_parse_check(int argc, const char *argv[])
{
	if (argc < 2) {
		LOG_W(log_misc, "missing filename");
		return false;
	}

	struct tokens toks = { 0 };
	struct ast ast = { 0 };
	if (!lexer_lex(language_internal, &toks, argv[1])) {
		return false;
	} else if (!parser_parse(&ast, &toks)) {
		return false;
	}

	return true;
}

static bool
cmd_ast(int argc, const char *argv[])
{
	if (argc < 2) {
		LOG_W(log_misc, "missing filename");
		return false;
	}

	struct tokens toks = { 0 };
	struct ast ast = { 0 };
	if (!lexer_lex(language_internal, &toks, argv[1])) {
		return false;
	} else if (!parser_parse(&ast, &toks)) {
		return false;
	}

	print_ast(&ast);
	return true;
}

static bool
cmd_eval(int argc, const char *argv[])
{
	if (argc < 2) {
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
	ret = eval(&wk, argv[1]);

	workspace_destroy(&wk); // just to test for memory leaks in valgrind
	return ret;
}

int
main(int argc, char *argv[])
{
	log_init();
	log_set_lvl(log_debug);
	log_set_filters(0xffffffff & (~log_filter_to_bit(log_mem)));

	uint32_t i;
	static struct {
		const char *name;
		bool (*cmd)(int, const char*[]);
	} commands[] = {
		{ "setup", cmd_setup },
		{ "eval", cmd_eval },
		{ "ast", cmd_ast },
		{ "parse_check", cmd_parse_check },
		{ "samu", cmd_samu },
		{ 0 },
	};

	if (argc < 2) {
		LOG_W(log_misc, "missing command");
		goto print_commands;
	}

	const char *cmd = argv[1];

	for (i = 0; commands[i].name; ++i) {
		if (strcmp(commands[i].name, cmd) == 0) {
			if (!commands[i].cmd(argc - 1, (const char **)&argv[1])) {
				return 1;
			}
			return 0;
		}
	}

	if (!commands[i].name) {
		LOG_W(log_misc, "unknown command '%s'", argv[1]);
		goto print_commands;
	}

	return 0;

print_commands:
	LOG_I(log_misc, "avaliable commands:");
	for (i = 0; commands[i].name; ++i) {
		LOG_I(log_misc, "  %s", commands[i].name);
	}
	return 1;
}
