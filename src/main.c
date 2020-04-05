#include "command.h"

int
main(int argc, char **argv) {
	const struct command *command = get_command("setup");
	if (argc > 2) {
		command = get_command(argv[1]);
	}

	return command->execute(argc, argv);
}
