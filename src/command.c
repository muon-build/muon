#define _XOPEN_SOURCE 500

#include "command.h"
#include "getopt_long.h"
#include "parse.h"

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <errno.h>

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

	const char *builddir = NULL;
	const char *sourcedir = NULL;

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
			if (builddir == NULL) {
				builddir = optarg;
				break;
			}
			if (sourcedir == NULL) {
				sourcedir = optarg;
				break;
			}
			break;
		default:
			fprintf(stderr, "%s: unrecognized option: %c\n", argv[0], opt);
			return 1;
		}
	}

	if (mkdir(builddir, S_IRUSR | S_IWUSR) == -1) {
		fprintf(stderr, "Build directory already configured\n");
		return 1;
	}

	if (sourcedir == NULL) {
		sourcedir = ".";
	}

	char *abs_builddir = realpath(builddir, NULL);
	char *abs_sourcedir = realpath(sourcedir, NULL);

	int rc = parse(abs_sourcedir);

	free(abs_builddir);
	free(abs_sourcedir);

	return rc;
}


static const struct command commands[] = {
	{"setup", setup},
};

const struct command *
get_command(const char *name)
{
	const size_t len_commands = (sizeof(commands) / sizeof(commands[0]));
	for (size_t i = 0; i < len_commands; ++i) {
		if (strcmp(name, commands[i].name) == 0) {
			return &commands[i];
		}
	}

	return NULL;
}
