#include "token.h"
#include "lexer.h"
#include "log.h"

#include <stdbool.h>
#include <string.h>

struct token
token_create(struct lexer *lexer, enum token_type type)
{
	return (struct token) {
		.type = type,
		.data = lexer->start,
		.len = lexer->current - lexer->start,
	};
}

static bool
keyword_cmp(struct lexer *lexer, const char *keyword)
{
	return memcmp(lexer->start, keyword, strlen(keyword)) == 0;
}


struct token
token_create_identifier(struct lexer *lexer)
{
	enum token_type type = TOKEN_ERROR;
	switch (lexer->start[0]) {
	case 't':
		if (keyword_cmp(lexer, "true")) {
			type = TOKEN_TRUE;
		}
		break;
	default:
		type = TOKEN_IDENTIFIER;
	}

	return token_create(lexer, type);
}

struct token
token_error(const char *msg)
{
	return (struct token) {
		.type = TOKEN_ERROR,
		.data = msg,
		.len = strlen(msg),
	};
}

const char *token_to_string(struct token *token)
{
#define TOKEN_TRANSLATE(e) case e: return #e;
	switch (token->type) {
	TOKEN_TRANSLATE(TOKEN_LPAREN);
	TOKEN_TRANSLATE(TOKEN_RPAREN);
	TOKEN_TRANSLATE(TOKEN_LBRACKET);
	TOKEN_TRANSLATE(TOKEN_RBRACKET);
	TOKEN_TRANSLATE(TOKEN_LCURL);
	TOKEN_TRANSLATE(TOKEN_RCURL);
	TOKEN_TRANSLATE(TOKEN_DOT);
	TOKEN_TRANSLATE(TOKEN_COMMA);
	TOKEN_TRANSLATE(TOKEN_COLON);
	TOKEN_TRANSLATE(TOKEN_PLUS);
	TOKEN_TRANSLATE(TOKEN_MINUS);
	TOKEN_TRANSLATE(TOKEN_STAR);
	TOKEN_TRANSLATE(TOKEN_SLASH);
	TOKEN_TRANSLATE(TOKEN_MODULO);
	TOKEN_TRANSLATE(TOKEN_ASSIGN);
	TOKEN_TRANSLATE(TOKEN_EQ);
	TOKEN_TRANSLATE(TOKEN_NEQ);
	TOKEN_TRANSLATE(TOKEN_GT);
	TOKEN_TRANSLATE(TOKEN_GEQ);
	TOKEN_TRANSLATE(TOKEN_LT);
	TOKEN_TRANSLATE(TOKEN_LEQ);
	TOKEN_TRANSLATE(TOKEN_TRUE);
	TOKEN_TRANSLATE(TOKEN_FALSE);
	TOKEN_TRANSLATE(TOKEN_IF);
	TOKEN_TRANSLATE(TOKEN_ELSE);
	TOKEN_TRANSLATE(TOKEN_ELIF);
	TOKEN_TRANSLATE(TOKEN_ENDIF);
	TOKEN_TRANSLATE(TOKEN_AND);
	TOKEN_TRANSLATE(TOKEN_OR);
	TOKEN_TRANSLATE(TOKEN_NOT);
	TOKEN_TRANSLATE(TOKEN_FOREACH);
	TOKEN_TRANSLATE(TOKEN_ENDFOREACH);
	TOKEN_TRANSLATE(TOKEN_IN);
	TOKEN_TRANSLATE(TOKEN_CONTINUE);
	TOKEN_TRANSLATE(TOKEN_BREAK);
	TOKEN_TRANSLATE(TOKEN_IDENTIFIER);
	TOKEN_TRANSLATE(TOKEN_STRING);
	TOKEN_TRANSLATE(TOKEN_NUMBER);
	TOKEN_TRANSLATE(TOKEN_EOL);
	TOKEN_TRANSLATE(TOKEN_IGNORE);
	TOKEN_TRANSLATE(TOKEN_EOF);
	TOKEN_TRANSLATE(TOKEN_ERROR);
	default:
		report("unknown token");
		break;
	}
#undef TOKEN_TRANSLATE
	return "";
}
