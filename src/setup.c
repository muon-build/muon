#define _XOPEN_SOURCE 500

#include "setup.h"
#include "log.h"
#include "getopt_long.h"
#include "parser.h"
#include "interpreter.h"
#include "ninja.h"

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <errno.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int
setup_usage(void)
{
	printf("usage: boson setup [options] builddir [sourcedir]\n"
			"options:\n"
			"  -h, --help\t\tDisplay this message and exit\n"
			"\n"
			"builddir\t\tDirectory into which the file will be generated, "
			"required\n"
			"sourcedir\t\tDirectory, optional\n"
			"\tDefault to the current working directory\n");
	return 0;
}

int
setup(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Must specify a build directory\n");
		return 1;
	}

	static const struct option options[] = {
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0},
	};

	const char *build_dir = NULL;
	const char *source_dir = NULL;

	optind = 1;
	int opt;
	while ((opt = getopt_long(argc, argv, "-h", options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			return setup_usage();
		case '?':
			return 1;
		case 1:
			if (strcmp(optarg, "setup") == 0) {
				break;
			}
			if (build_dir == NULL) {
				build_dir = optarg;
				break;
			}
			if (source_dir == NULL) {
				source_dir = optarg;
				break;
			}
			break;
		default:
			fprintf(stderr, "%s: unrecognized option: %c\n", argv[0], opt);
			return 1;
		}
	}

	if (source_dir == NULL) {
		source_dir = ".";
	}

	char *cwd = calloc(PATH_MAX, sizeof(char));
	getcwd(cwd, PATH_MAX);

	char abs_source_dir[PATH_MAX] = {0}, abs_build_dir[PATH_MAX] = {0};
	realpath(source_dir, abs_source_dir);
	snprintf(abs_build_dir, PATH_MAX, "%s/%s", cwd, build_dir);
	free(cwd);

	info("Version: " VERSION);

	struct ast_root root = parse(abs_source_dir);

	struct context ctx = interpret_ast(&root);

	// TODO free ctx
	return emit_ninja(&ctx, abs_build_dir);
}
