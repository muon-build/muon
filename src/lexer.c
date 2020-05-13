#include "lexer.h"
#include "token.h"
#include "log.h"

#include <stdbool.h>
#include <stdio.h>

static bool
at_end(struct lexer *lexer)
{
	return *lexer->current == '\0';
}

static bool
is_alpha(char c)
{
	return (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c == '_');
}

static bool
is_digit(char c)
{
	return (c >= '0' && c <= '9');
}

static char
advance(struct lexer *lexer)
{
	lexer->current++;
	return lexer->current[-1];
}

static char
peek(struct lexer *lexer)
{
	return *lexer->current;
}

static struct token
tokenize_identifier(struct lexer *lexer)
{
	while (is_alpha(peek(lexer)) || is_digit(peek(lexer))) {
		advance(lexer);
	}

	return token_create_identifier(lexer);
}

static struct token
tokenize_string(struct lexer *lexer)
{
	while (peek(lexer) != '\'' && !at_end(lexer)) {
		advance(lexer);
	}

	if (at_end(lexer)) {
		return token_error("Unterminated string");
	}

	// closing quote
	advance(lexer);
	return token_create(lexer, TOKEN_STRING);
}

void
skip_whitespaces(struct lexer *lexer)
{
	for (;;) {
		switch(peek(lexer)) {
		case '#':
			while(peek(lexer) != '\n') {
				advance(lexer);
			}
			lexer->line++;
			advance(lexer);
			break;
		case '\n':
			lexer->line++;
			advance(lexer);
			break;
		case ' ':
		case '\r':
		case '\t':
			advance(lexer);
			break;
		default:
			return;
		}
	}
}

struct token
lexer_tokenize(struct lexer *lexer)
{
	skip_whitespaces(lexer);

	lexer->start = lexer->current;

	char c = advance(lexer);
	if (at_end(lexer)) {
		return token_create(lexer, TOKEN_EOF);
	} else if (is_alpha(c)) {
		return tokenize_identifier(lexer);
	} else if (is_digit(c)) {
		//return token_digit(lexer);
	}

	switch (c) {
		case '(':
			return token_create(lexer, TOKEN_LPAREN);
		case ')':
			return token_create(lexer, TOKEN_RPAREN);
		case '[':
			return token_create(lexer, TOKEN_LBRACKET);
		case ']':
			return token_create(lexer, TOKEN_RBRACKET);
		case '.':
			return token_create(lexer, TOKEN_DOT);
		case ',':
			return token_create(lexer, TOKEN_COMMA);
		case ':':
			return token_create(lexer, TOKEN_COLON);
		// arithmetic
		case '=':
			return token_create(lexer, TOKEN_ASSIGN);
		case '\'':
			return tokenize_string(lexer);
	}

	return token_error("");
}
