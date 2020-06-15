#include "lexer.h"
#include "token.h"
#include "log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

static int
next(struct lexer *lexer)
{
	if (lexer->cur == '\n') {
		lexer->line++;
		lexer->col = 1;
	} else {
		lexer->col++;
	}

	lexer->cur = getc(lexer->file);
	//printf("next %c\n", lexer->cur);
	return lexer->cur;
}

static int
peek(struct lexer *lexer)
{
	int c = getc(lexer->file);
	ungetc(c, lexer->file);
	return c;
}

static void
comment(struct lexer *lexer)
{
	if (lexer->cur != '#') {
		return;
	}

	do {
		next(lexer);
	} while (lexer->cur != '\n');
	next(lexer);
}

static struct token
identifier(struct lexer *lexer)
{
	struct token token = {
		.type = TOKEN_IDENTIFIER,
	};

	size_t n = 1;
	char *id = calloc(n, sizeof(char));
	if (id == NULL) {
		fatal("failed to allocate buffer for identifier");
	}

	id[n - 1] = lexer->cur;
	while (isalnum(next(lexer)) || lexer->cur == '_') {
		++n;
		id = realloc(id, n);
		id[n - 1] = lexer->cur;
	}

	token.data = id;
	token.len = n;

	return token;
}

static struct token
number(struct lexer *lexer)
{
	fatal("todo number");

	struct token token = {
		.type = TOKEN_NUMBER,
	};

	/* FIXME handle octal */
	/* FIXME handle hexadecimal */
	/*
	if (lexer->cur == '0') {
		if (peek(lexer) == 'o')
		else if (peek(lexer) == 'x')
	}
	*/

	return token;
}

static struct token
string(struct lexer *lexer)
{
	struct token token = {
		.type = TOKEN_STRING,
	};

	while(lexer->cur == '\'') {
		next(lexer);
	}

	size_t n = 1;
	char *id = calloc(n, sizeof(char));
	if (id == NULL) {
		fatal("failed to allocate buffer for identifier");
	}

	id[n - 1] = lexer->cur;
	while (next(lexer) != '\'') {
		++n;
		id = realloc(id, n);
		id[n - 1] = lexer->cur;
	}

	token.data = id;
	token.len = n;

	while(lexer->cur == '\'') {
		next(lexer);
	}

	return token;
}

struct token
lexer_tokenize(struct lexer *lexer)
{
	while (isspace(lexer->cur)) {
		next(lexer);
	}

	if (lexer->cur == '#') {
		comment(lexer);
	}

	if (isalnum(lexer->cur) || lexer->cur == '_') {
		return identifier(lexer);
	} else if (isdigit(lexer->cur)) {
		return number(lexer);
	} else if (lexer->cur == '\'') {
		return string(lexer);
	}

	struct token token = {0};
	switch(lexer->cur) {
		case '(':
			token.type = TOKEN_LPAREN;
			break;
		case ')':
			token.type = TOKEN_RPAREN;
			break;
		case '[':
			token.type = TOKEN_LBRACK;
			break;
		case ']':
			token.type = TOKEN_RBRACK;
			break;
		case '{':
			token.type = TOKEN_LCURL;
			break;
		case '}':
			token.type = TOKEN_RCURL;
			break;
		case '.':
			token.type = TOKEN_DOT;
			break;
		case ',':
			token.type = TOKEN_COMMA;
			break;
		case ':':
			token.type = TOKEN_COLON;
			break;
		// arithmetic
		case '+':
			if (peek(lexer) == '=') {
				next(lexer);
				token.type = TOKEN_PLUS;
			} else {
				token.type = TOKEN_PLUS;
			}
			break;
		case '-':
			token.type = TOKEN_MINUS;
			break;
		case '\0':
		default:
			token.type = TOKEN_EOF;
			break;
	}
	next(lexer);
	return token;
}

void
lexer_init(struct lexer *lexer, const char *path)
{
	lexer->file = fopen(path, "r");
	if (lexer->file == NULL) {
		fatal("Failed to open %s", path);
	}

	lexer->path = path;
	lexer->line = 1;
	lexer->col = 1;

	next(lexer);
}

void
lexer_finish(struct lexer *lexer)
{
	fclose(lexer->file);
}
