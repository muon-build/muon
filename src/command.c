#include "command.h"
#include "setup.h"

#include <stddef.h>
#include <string.h>


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
