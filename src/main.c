#include "posix.h"

#include <limits.h>
#include <string.h>

#include "log.h"
#include "parser.h"
#include "interpreter.h"

static bool
cmd_setup(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Must specify a build directory\n");
		return 1;
	}

	const char *build_dir = argv[1];
	/* const char *source_dir = "."; */

	char cwd[PATH_MAX + 1] = { 0 };
	getcwd(cwd, PATH_MAX);

	char abs_source_dir[PATH_MAX + 1] = { 0 }, abs_build_dir[PATH_MAX + 1] = { 0 };
	/* realpath(source_dir, abs_source_dir); */
	uint32_t l;
	strncpy(abs_build_dir, cwd, PATH_MAX);
	l = strlen(cwd);
	assert(l < PATH_MAX);
	abs_build_dir[l] = '/';
	++l;
	assert(l < PATH_MAX);
	strncpy(abs_build_dir, build_dir, PATH_MAX - l);

#ifndef VERSION
#define VERSION ""
#endif
	LOG_I(log_misc, "Version: %s", VERSION);

	struct ast ast = { 0 };
	if (!parse(&ast, abs_source_dir)) {
		return 1;
	}

	uint32_t i;
	for (i = 0; i < ast.ast.len; ++i) {
		print_tree(&ast, *(uint32_t *)darr_get(&ast.ast, i), 0);
	}

	struct workspace wk = { 0 };
	if (!interpret(&ast, &wk)) {
		return 1;
	}

	/* struct context ctx = interpret_ast(&ast); */

	/* // TODO free ctx */
	/* return emit_ninja(&ctx, abs_build_dir); */
	return 0;
}

int
main(int argc, char **argv)
{
	log_init();
	log_set_lvl(log_debug);
	log_set_filters(0xffffffff);

	static const struct {
		const char *name;
		bool (*cmd)(int, char*[]);
	} commands[] = {
		{ "setup", cmd_setup },
		{ 0 },
	};

	if (argc < 2) {
		LOG_W(log_misc, "command unspecified");
		return 1;
	}

	uint32_t i;
	for (i = 0; commands[i].name; ++i) {
		if (strcmp(commands[i].name, argv[1])) {
			if (!commands[i].cmd(argc - 1, &argv[2])) {
				return 1;
			}
		}
	}

	if (!commands[i].name) {
		LOG_W(log_misc, "unknown command '%s'", argv[1]);
		return 1;
	}

	return 0;
}
