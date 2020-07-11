#ifndef BOSON_LEXER_H
#define BOSON_LEXER_H

#include <stdio.h>

struct token;

struct lexer {
	FILE *file;
	const char *path;
	int cur, line, col;
};

struct token *lexer_tokenize(struct lexer *);

void lexer_init(struct lexer *, const char *);
void lexer_finish(struct lexer *);

#endif // BOSON_LEXER_H
