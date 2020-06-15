#include "parse.h"
#include "lexer.h"
#include "token.h"
#include "log.h"

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>

#define PATH_MAX 4096

int
parse(const char *source_dir)
{
	char source_path[PATH_MAX] = {0};
	sprintf(source_path, "%s/%s", source_dir, "meson.build");

	struct lexer lexer = {0};
	lexer_init(&lexer, source_path);

	struct token token = {0};
	for (;;) {
		token = lexer_tokenize(&lexer);
		info("%s", token_to_string(&token));

		if (token.type == TOKEN_EOF) {
			break;
		} else if (token.type == TOKEN_IDENTIFIER) {
			info("identifier %s", token.data);
		} else if (token.type == TOKEN_NUMBER) {
			info("number %s", token.data);
		} else if (token.type == TOKEN_STRING) {
			info("string %s", token.data);
		}
	}

	lexer_finish(&lexer);

	return 0;
}
