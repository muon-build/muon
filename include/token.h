#ifndef BOSON_TOKEN_H
#define BOSON_TOKEN_H

#include <stddef.h>

enum token_type {
	TOKEN_LPAREN,
	TOKEN_RPAREN,
	TOKEN_LBRACKET,
	TOKEN_RBRACKET,
	TOKEN_LCURL,
	TOKEN_RCURL,
	TOKEN_DOT,
	TOKEN_COMMA,
	TOKEN_COLON,

	// arithmetic
	TOKEN_PLUS,
	TOKEN_MINUS,
	TOKEN_STAR,
	TOKEN_SLASH,
	TOKEN_MODULO,
	TOKEN_ASSIGN,

	// relational
	TOKEN_EQ,
	TOKEN_NEQ,
	TOKEN_GT,
	TOKEN_GEQ,
	TOKEN_LT,
	TOKEN_LEQ,

	// keyword
	TOKEN_TRUE,
	TOKEN_FALSE,
	TOKEN_IF,
	TOKEN_ELSE,
	TOKEN_ELIF,
	TOKEN_ENDIF,
	TOKEN_AND,
	TOKEN_OR,
	TOKEN_NOT,
	TOKEN_FOREACH,
	TOKEN_ENDFOREACH,
	TOKEN_IN,
	TOKEN_CONTINUE,
	TOKEN_BREAK,

	// literal
	TOKEN_IDENTIFIER,
	TOKEN_STRING,
	TOKEN_NUMBER,

	TOKEN_EOL,
	TOKEN_IGNORE,
	TOKEN_EOF,
	TOKEN_ERROR,
};

struct token {
	enum token_type type;
	const char *data;
	size_t len;
};

struct lexer;

struct token token_create(struct lexer *, enum token_type);
struct token token_create_identifier(struct lexer *);
struct token token_error(const char *);

const char *token_to_string(struct token*);

#endif // BOSON_TOKEN_H
