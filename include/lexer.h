#ifndef BOSON_LEXER_H
#define BOSON_LEXER_H

#include <stdbool.h>
#include <stdint.h>

#include "darr.h"

enum token_type {
	tok_eof,
	tok_eol,
	tok_lparen,
	tok_rparen,
	tok_lbrack,
	tok_rbrack,
	tok_lcurl,
	tok_rcurl,
	tok_dot,
	tok_comma,
	tok_colon,
	tok_assign,
	tok_plus,
	tok_minus,
	tok_star,
	tok_slash,
	tok_modulo,
	tok_pluseq,
	tok_mineq,
	tok_stareq,
	tok_slasheq,
	tok_modeq,
	tok_eq,
	tok_neq,
	tok_gt,
	tok_geq,
	tok_lt,
	tok_leq,
	tok_true,
	tok_false,
	tok_if,
	tok_else,
	tok_elif,
	tok_endif,
	tok_and,
	tok_or,
	tok_not,
	tok_qm,
	tok_foreach,
	tok_endforeach,
	tok_in,
	tok_continue,
	tok_break,
	tok_identifier,
	tok_string,
	tok_number,
	tok_question_mark,
};

#define TOKEN_MAX_DATA 64

struct token {
	char data[TOKEN_MAX_DATA + 1];
	enum token_type type;
	uint32_t n, line, col;
};

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

const char *token_type_to_string(enum token_type);
const char *token_to_string(struct token *);

#endif // BOSON_LEXER_H
