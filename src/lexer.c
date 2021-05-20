#include "posix.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

#include "filesystem.h"
#include "lexer.h"
#include "log.h"
#include "mem.h"

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

static struct token *
next_tok(struct lexer *l)
{
	uint32_t idx = darr_push(&l->tok, &(struct token){ 0 });
	return darr_get(&l->tok, idx);
}

static bool
is_valid_start_of_identifier(const char c)
{
	return c == '_' || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

static bool
is_digit(const char c)
{
	return '0' <= c && c <= '9';
}

static bool
is_valid_inside_of_identifier(const char c)
{
	return is_valid_start_of_identifier(c) || is_digit(c);
}

static bool
is_skipchar(const char c)
{
	return c == ' ' || c == '\t' || c == '#';
}

static bool
keyword(struct lexer *lexer, struct token *token)
{
	/* must stay in sorted order */
	static const struct {
		const char *name;
		enum token_type type;
	} keywords[] = {
		{ "and", tok_and },
		{ "break", tok_break },
		{ "continue", tok_continue },
		{ "elif", tok_elif },
		{ "else", tok_else },
		{ "endforeach", tok_endforeach },
		{ "endif", tok_endif },
		{ "false", tok_false },
		{ "foreach", tok_foreach },
		{ "if", tok_if },
		{ "in", tok_in },
		{ "not", tok_not },
		{ "or", tok_or },
		{ "true", tok_true },
	};

	int low = 0, high = (sizeof(keywords) / sizeof(keywords[0])) - 1, mid, cmp;

	while (low <= high) {
		mid = (low + high) / 2;
		cmp = strcmp(token->data, keywords[mid].name);
		if (cmp == 0) {
			token->type = keywords[mid].type;
			return true;
		}

		if (cmp < 0) {
			high = mid - 1;
		} else {
			low = mid + 1;
		}
	}

	return false;
}

static void
number(struct lexer *lexer, struct token *token)
{
	LOG_W(log_lex, "todo number");

	token->type = tok_number;

	/* FIXME handle octal */
	/* FIXME handle hexadecimal */
	/*
	   if (lexer->cur == '0') {
	        if (peek(lexer) == 'o')
	        else if (peek(lexer) == 'x')
	   }
	 */
}

static void
copy_into_token_data(struct lexer *lexer, struct token *tok, uint32_t start, uint32_t end)
{
	if (end == start) {
		return;
	}

	assert(end > start);

	tok->n = end - start;

	tok->data = &lexer->data[start];
}

static void
identifier(struct lexer *lexer, struct token *token)
{
	uint32_t start = lexer->i;

	while (is_valid_inside_of_identifier(lexer->data[lexer->i])) {
		++lexer->i;
	}

	copy_into_token_data(lexer, token, start, lexer->i);

	if (!keyword(lexer, token)) {
		token->type = tok_identifier;
	}
}

static void
string(struct lexer *lexer, struct token *token)
{
	token->type = tok_string;

	++lexer->i;

	uint32_t start = lexer->i;

	while (lexer->data[lexer->i] != '\'') {
		++lexer->i;
	}

	copy_into_token_data(lexer, token, start, lexer->i);

	++lexer->i;
}

enum lex_result {
	lex_cont,
	lex_done,
	lex_fail,
};

static enum lex_result
lexer_tokenize_one(struct lexer *lexer)
{
	struct token *token = next_tok(lexer);

	token->data = NULL;
	token->n = 0;
	token->line = lexer->line;
	token->col = lexer->i - lexer->line_start + 1;

	while (is_skipchar(lexer->data[lexer->i])) {
		if (lexer->data[lexer->i] == '#') {
			while (lexer->data[lexer->i] && lexer->data[lexer->i] != '\n') {
				++lexer->i;
			}

			assert(lexer->data[lexer->i] == 0 || lexer->data[lexer->i] == '\n');
			if (lexer->data[lexer->i]) {
				++lexer->line;
				lexer->line_start = lexer->i + 1;
			}
		}
		++lexer->i;
	}

	if (is_valid_start_of_identifier(lexer->data[lexer->i])) {
		identifier(lexer, token);
	} else if (is_digit(lexer->data[lexer->i])) {
		number(lexer, token);
	} else if (lexer->data[lexer->i] == '\'') {
		string(lexer, token);
	} else {
		switch (lexer->data[lexer->i]) {
		case '\n':
			++lexer->line;
			lexer->line_start = lexer->i + 1;
			if (lexer->enclosing.paren || lexer->enclosing.bracket
			    || lexer->enclosing.curl) {
				goto skip;
			}

			token->type = tok_eol;
			break;
		case '(':
			++lexer->enclosing.paren;
			token->type = tok_lparen;
			break;
		case ')':
			if (!lexer->enclosing.paren) {
				return lex_fail;
			}
			--lexer->enclosing.paren;

			token->type = tok_rparen;
			break;
		case '[':
			++lexer->enclosing.bracket;
			token->type = tok_lbrack;
			break;
		case ']':
			if (!lexer->enclosing.bracket) {
				return lex_fail;
			}
			--lexer->enclosing.bracket;

			token->type = tok_rbrack;
			break;
		case '{':
			++lexer->enclosing.curl;
			token->type = tok_lcurl;
			break;
		case '}':
			if (!lexer->enclosing.curl) {
				return lex_fail;
			}
			--lexer->enclosing.curl;

			token->type = tok_rcurl;
			break;
		case '.':
			token->type = tok_dot;
			break;
		case ',':
			token->type = tok_comma;
			break;
		case ':':
			token->type = tok_colon;
			break;
		case '?':
			token->type = tok_question_mark;
			break;
		// arithmetic
		case '+':
			if (lexer->data[lexer->i + 1] == '=') {
				++lexer->i;
				token->type = tok_pluseq;
			} else {
				token->type = tok_plus;
			}
			break;
		case '-':
			token->type = tok_minus;
			break;
		case '=':
			token->type = tok_assign;
			break;
		case '\0':
			token->type = tok_eof;
			return lex_done;
		default:
			LOG_W(log_lex, "unexpected character: '%c'", lexer->data[lexer->i]);
			return lex_fail;
		}

		++lexer->i;
	}

	return lex_cont;
skip:
	++lexer->i;
	--lexer->tok.len;
	return lex_cont;
}


bool
lexer_tokenize(struct lexer *lexer)
{
	uint32_t i;
	struct token *tok;

	while (true) {
		switch (lexer_tokenize_one(lexer)) {
		case lex_cont:
			break;
		case lex_fail:
			return false;
		case lex_done:
			goto done;
		}
	}

done:
	for (i = 0; i < lexer->tok.len; ++i) {
		tok = darr_get(&lexer->tok, i);

		switch (tok->type) {
		case tok_string:
		case tok_identifier:
			lexer->data[(tok->data - lexer->data) + tok->n] = 0;
			break;
		default:
			break;
		}
	}

	return true;
}

bool
lexer_init(struct lexer *lexer, const char *path)
{
	*lexer = (struct lexer) {
		.path = path,
		.line = 1,
	};

	darr_init(&lexer->tok, 2048, sizeof(struct token));

	if (!fs_read_entire_file(path, &lexer->data, &lexer->data_len)) {
		goto err;
	}

	return true;
err:
	lexer_finish(lexer);
	return false;
}

void
lexer_finish(struct lexer *lexer)
{
	if (lexer->data) {
		z_free(lexer->data);
	}

	darr_destroy(&lexer->tok);
}
