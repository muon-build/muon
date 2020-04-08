#include "command.h"
#include "getopt_long.h"

#include <stddef.h>
#include <string.h>
#include <stdio.h>

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
setup(int argc, char *argv[])
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
			printf("boson " VERSION "\n");
			return 0;
		case 'h':
			return setup_usage();
		default:
			printf("default %d\n", opt);
			break;
		}
	}

	if (argc < 2) {
		fprintf(stderr, "Must specify a build directory\n");
		return 1;
	}

	//const char *builddir = NULL;
	//const char *sourcedir = ".";
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "setup") == 0) {
			continue;
		}
	}

	return 0;
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
