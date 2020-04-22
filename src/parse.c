#include "parse.h"
#include "lexer.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>

static char *
read_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		fatal("Failed to open '%s'", path);
	}

	fseek(file, 0L, SEEK_END);
	size_t file_size = ftell(file);
	rewind(file);

	char *buffer = calloc(file_size + 1, sizeof(char));
	if (buffer == NULL) {
		fatal("Failed to read '%s'", path);
	}

	size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
	if (bytes_read < file_size) {
		fatal("Error while reading '%s'", path);
	}

	buffer[bytes_read] = '\0';

	fclose(file);
	return buffer;
}

int
parse(const char *source_dir)
{
	char source_path[PATH_MAX] = {0};
	sprintf(source_path, "%s/%s", source_dir, "meson.build");

	char *data = read_file(source_path);

	struct lexer lexer = {
		.start = data,
		.current = data,
		.line = 1,
	};

	lexer_tokenize(&lexer);

	free(data);

	return 0;
}
