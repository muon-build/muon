#include "posix.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#include "filesystem.h"
#include "interpreter.h"
#include "log.h"
#include "output.h"
#include "output.h"
#include "parser.h"

#define BUF_LEN 256

static bool
cmd_setup(int argc, char **argv)
{
	if (argc < 1) {
		LOG_W(log_misc, "missing build directory\n");
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
	snprintf(build, PATH_MAX, "%s/%s", cwd, argv[0]);

	LOG_I(log_misc, "source: %s, build: %s", source, build);

	struct ast ast = { 0 };
	if (!parse(&ast, source)) {
		return false;
	}

	/* uint32_t i; */
	/* for (i = 0; i < ast.ast.len; ++i) { */
	/* 	print_tree(&ast, *(uint32_t *)darr_get(&ast.ast, i), 0); */
	/* } */

	struct workspace wk = { .cwd = cwd, .build_dir = build };
	workspace_init(&wk);

	if (!interpret(&ast, &wk)) {
		return false;
	}

	output_build(&wk, build);

	return true;
}

int
main(int argc, char **argv)
{
	log_init();
	log_set_lvl(log_debug);
	log_set_filters(0xffffffff & (~log_filter_to_bit(log_mem)));

	static const struct {
		const char *name;
		bool (*cmd)(int, char*[]);
	} commands[] = {
		{ "setup", cmd_setup },
		{ 0 },
	};

	if (argc < 2) {
		LOG_W(log_misc, "missing command");
		return 1;
	}

	uint32_t i;
	for (i = 0; commands[i].name; ++i) {
		if (strcmp(commands[i].name, argv[1]) == 0) {
			if (!commands[i].cmd(argc - 2, &argv[2])) {
				return 1;
			}
			return 0;
		}
	}

	if (!commands[i].name) {
		LOG_W(log_misc, "unknown command '%s'", argv[1]);
		return 1;
	}

	return 0;
}
