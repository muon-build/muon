#ifndef BOSON_LEXER_H
#define BOSON_LEXER_H

#include <stdbool.h>
#include <stdint.h>

#include "darr.h"
#include "eval.h"

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
	tok_question_mark,

	/* math */
	tok_plus,
	tok_minus,
	tok_star,
	tok_slash,
	tok_modulo,

	/* assign */
	tok_assign,
	tok_plus_assign,

	/* comparison */
	tok_eq,
	tok_neq,
	tok_gt,
	tok_geq,
	tok_lt,
	tok_leq,

	/* keywords */
	tok_if,
	tok_else,
	tok_elif,
	tok_endif,
	tok_and,
	tok_or,
	tok_not,
	tok_foreach,
	tok_endforeach,
	tok_in,
	tok_continue,
	tok_break,

	/* internal keywords */
	tok_def,
	tok_end,

	/* literals */
	tok_identifier,
	tok_string,
	tok_number,
	tok_true,
	tok_false,
};

struct token {
	union {
		const char *s;
		int64_t n;
	} dat;
	enum token_type type;
	uint32_t n, line, col;
};

struct tokens {
	struct darr tok;
	const char *src_path;
	char *data;
	uint64_t data_len;
};

bool lexer_lex(enum language_mode lang_mode, struct tokens *toks, const char *path);
void tokens_destroy(struct tokens *toks);

const char *tok_type_to_s(enum token_type type);
const char *tok_to_s(struct token *token);
#endif
