#include "parse.h"
#include "lexer.h"
#include "token.h"
#include "log.h"

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

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

	struct token tok = {0};
	for (;;) {
		tok = lexer_tokenize(&lexer);
		info("token %s : ", token_to_string(&tok), tok.data);

		if (tok.type == TOKEN_EOF) {
			break;
		}
	}

	free(data);

	return 0;
}
