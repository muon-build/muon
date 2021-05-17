#include "token.h"
#include "log.h"

#include <stdio.h>

const char *
token_type_to_string(enum token_type type)
{
#define TOKEN_TRANSLATE(e) case e: return #e;
	switch (type) {
		TOKEN_TRANSLATE(tok_eof);
		TOKEN_TRANSLATE(tok_eol);
		TOKEN_TRANSLATE(tok_lparen);
		TOKEN_TRANSLATE(tok_rparen);
		TOKEN_TRANSLATE(tok_lbrack);
		TOKEN_TRANSLATE(tok_rbrack);
		TOKEN_TRANSLATE(tok_lcurl);
		TOKEN_TRANSLATE(tok_rcurl);
		TOKEN_TRANSLATE(tok_dot);
		TOKEN_TRANSLATE(tok_comma);
		TOKEN_TRANSLATE(tok_colon);
		TOKEN_TRANSLATE(tok_assign);
		TOKEN_TRANSLATE(tok_plus);
		TOKEN_TRANSLATE(tok_minus);
		TOKEN_TRANSLATE(tok_star);
		TOKEN_TRANSLATE(tok_slash);
		TOKEN_TRANSLATE(tok_modulo);
		TOKEN_TRANSLATE(tok_pluseq);
		TOKEN_TRANSLATE(tok_mineq);
		TOKEN_TRANSLATE(tok_stareq);
		TOKEN_TRANSLATE(tok_slasheq);
		TOKEN_TRANSLATE(tok_modeq);
		TOKEN_TRANSLATE(tok_eq);
		TOKEN_TRANSLATE(tok_neq);
		TOKEN_TRANSLATE(tok_gt);
		TOKEN_TRANSLATE(tok_geq);
		TOKEN_TRANSLATE(tok_lt);
		TOKEN_TRANSLATE(tok_leq);
		TOKEN_TRANSLATE(tok_true);
		TOKEN_TRANSLATE(tok_false);
		TOKEN_TRANSLATE(tok_if);
		TOKEN_TRANSLATE(tok_else);
		TOKEN_TRANSLATE(tok_elif);
		TOKEN_TRANSLATE(tok_endif);
		TOKEN_TRANSLATE(tok_and);
		TOKEN_TRANSLATE(tok_or);
		TOKEN_TRANSLATE(tok_not);
		TOKEN_TRANSLATE(tok_qm);
		TOKEN_TRANSLATE(tok_foreach);
		TOKEN_TRANSLATE(tok_endforeach);
		TOKEN_TRANSLATE(tok_in);
		TOKEN_TRANSLATE(tok_continue);
		TOKEN_TRANSLATE(tok_break);
		TOKEN_TRANSLATE(tok_identifier);
		TOKEN_TRANSLATE(tok_string);
		TOKEN_TRANSLATE(tok_number);
	default:
		LOG_W(log_tok, "unknown token");
		break;
	}
#undef TOKEN_TRANSLATE
	return "";
}


#define BUF_LEN 256
const char *
token_to_string(struct token *tok)
{
	static char buf[BUF_LEN + 1];
	uint32_t i;

	i = snprintf(buf, BUF_LEN, "%s", token_type_to_string(tok->type));
	if (tok->n) {
		i += snprintf(&buf[i], BUF_LEN - i, ":'%s'", tok->data);
	}

	i += snprintf(&buf[i], BUF_LEN - i, " line %d, col: %d", tok->line, tok->col);

	return buf;
}
