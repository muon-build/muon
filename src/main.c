#include "log.h"
#include "setup.h"

#include <stdio.h>
#include <string.h>

struct command {
	const char *name;
	int (*execute)(int, char*[]);
};

static const struct command commands[] = {
	{ "setup", setup },
};

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
main(int argc, char **argv)
{
	log_init();
	log_set_lvl(log_debug);
	log_set_filters(0xffffffff);

	return get_command("setup")->execute(argc, argv);
}
