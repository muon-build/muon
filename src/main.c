#include "setup.h"
#include "getopt_long.h"

#include <stdio.h>
#include <string.h>

struct command {
	const char *name;
	int (*execute)(int, char*[]);
};

static const struct command commands[] = {
	{"setup", setup},
};

static int
usage(void)
{
	printf("usage: boson [options] [command] ...\n"
			"options:\n"
			"  -h, --help\t\tDisplay this message and exit\n"
			"\n"
			"command:\n"
			"  If no command is specified it defaults to setup\n"
			"  setup\t\tConfigure the build folder\n");
	return 0;
}

static const struct command *
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

int
main(int argc, char **argv) {
	static const struct option options[] = {
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0},
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "-h", options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			return usage();
		case '?':
			return 1;
		case 1:
			{
				const struct command *command = get_command(optarg);
				if (command) {
					return command->execute(argc, argv);
				}
			}
			break;
		default:
			fprintf(stderr, "%s: unrecognized option: %c\n", argv[0], opt);
			return 1;
		}
	}

	return get_command("setup")->execute(argc, argv);
}
