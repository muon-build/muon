#ifndef BOSON_LEXER_H
#define BOSON_LEXER_H

struct token;

struct lexer {
	const char *start;
	const char *current;
	int line;
};

struct token lexer_tokenize(struct lexer *);

#endif // BOSON_LEXER_H
