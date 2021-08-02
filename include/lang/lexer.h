#ifndef MUON_LEXER_H
#define MUON_LEXER_H

#include <stdbool.h>
#include <stdint.h>

#include "data/darr.h"
#include "lang/eval.h"

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

	/* literals */
	tok_identifier,
	tok_string,
	tok_number,
	tok_true,
	tok_false,
};

struct token {
	union token_data dat;
	enum token_type type;
	uint32_t n, line, col;
};

struct tokens {
	struct darr tok;
};

bool lexer_lex(struct tokens *toks, struct source_data *sdata, struct source *src);
void tokens_destroy(struct tokens *toks);

const char *tok_type_to_s(enum token_type type);
const char *tok_to_s(struct token *token);
#endif
