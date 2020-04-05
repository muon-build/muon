#ifndef BOSON_COMMAND_H
#define BOSON_COMMAND_H

struct command {
	const char *name;
	int (*execute)(int, char*[]);
};

const struct command *get_command(const char *name);

#endif // BOSON_COMMAND_H

