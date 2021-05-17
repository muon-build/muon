#ifndef BOSON_LEXER_H
#define BOSON_LEXER_H

#include <stdbool.h>
#include <stdint.h>

#include "darr.h"
#include "token.h"

struct lexer {
	const char *path;
	char *data;
	uint32_t i, line, line_start, data_len;
	struct {
		uint32_t paren, bracket, curl;
	} enclosing;
	struct darr tok;
};

bool lexer_tokenize(struct lexer *lexer);

bool lexer_init(struct lexer *lexer, const char *path);
void lexer_finish(struct lexer *);

#endif // BOSON_LEXER_H
