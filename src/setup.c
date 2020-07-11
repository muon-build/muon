#define _XOPEN_SOURCE 500

#include "setup.h"
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

	if (mkdir(build_dir, S_IRUSR | S_IWUSR) == -1) {
		fprintf(stderr, "Build directory already configured\n");
		return 1;
	}

	if (source_dir == NULL) {
		source_dir = ".";
	}

	char *abs_build_dir = realpath(build_dir, NULL);
	char *abs_source_dir = realpath(source_dir, NULL);

	printf("Version: " VERSION "\n");
	printf("Source dir: %s\n", abs_source_dir);
	printf("Build dir: %s\n", abs_build_dir);

	struct node *root = parse(abs_source_dir);

	free(abs_build_dir);
	free(abs_source_dir);

	free(root);

	return 0;
}
