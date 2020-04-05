#include "setup.h"
#include "getopt_long.h"

#include <stdio.h>
#include <stddef.h>

static int
usage(void)
{
	printf("usage: boson setup [--help] [--version] builddir [sourcedir]\n");
	return 0;
}

static int
version(void)
{
	printf("boson " VERSION "\n");
	return 0;
}

int setup(int argc, char *argv[])
{
	static const struct option options[] = {
		{"verbose", no_argument, 0, 'v'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0},
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "vh", options, NULL)) != -1) {
		switch (opt) {
		case 'v':
			return version();
		case 'h':
			return usage();
		}
	}

	if (argc < 2) {
		fprintf(stderr, "Must specify a build directory\n");
		return 1;
	}

	const char *builddir = argv[1];

	const char *sourcedir = NULL;
	if (argc >= 3) {
		sourcedir = argv[2];
	}

	return 0;
}
