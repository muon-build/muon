#ifndef BOSON_LEXER_H
#define BOSON_LEXER_H

struct lexer {
	const char *start;
	const char *current;
	int line;
};

void lexer_tokenize(struct lexer *);

#endif // BOSON_LEXER_H
