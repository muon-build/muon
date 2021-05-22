#include "posix.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "filesystem.h"
#include "lexer.h"
#include "log.h"
#include "mem.h"

enum lex_result {
	lex_cont,
	lex_done,
	lex_fail,
};

const char *
token_type_to_string(enum token_type type)
{
	switch (type) {
	case tok_eof: return "end of file";
	case tok_eol: return "end of line";
	case tok_lparen: return "(";
	case tok_rparen: return ")";
	case tok_lbrack: return "[";
	case tok_rbrack: return "]";
	case tok_lcurl: return "{";
	case tok_rcurl: return "}";
	case tok_dot: return ".";
	case tok_comma: return ",";
	case tok_colon: return ":";
	case tok_assign: return "=";
	case tok_plus: return "+";
	case tok_minus: return "-";
	case tok_star: return "*";
	case tok_slash: return "/";
	case tok_modulo: return "%";
	case tok_pluseq: return "+=";
	case tok_mineq: return "-=";
	case tok_stareq: return "*=";
	case tok_slasheq: return "/=";
	case tok_modeq: return "%=";
	case tok_eq: return "==";
	case tok_neq: return "!=";
	case tok_gt: return ">";
	case tok_geq: return ">=";
	case tok_lt: return "<";
	case tok_leq: return "<=";
	case tok_true: return "true";
	case tok_false: return "false";
	case tok_if: return "if";
	case tok_else: return "else";
	case tok_elif: return "elif";
	case tok_endif: return "endif";
	case tok_and: return "and";
	case tok_or: return "or";
	case tok_not: return "not";
	case tok_qm: return "tok_qm"; // ??
	case tok_foreach: return "foreach";
	case tok_endforeach: return "endforeach";
	case tok_in: return "in";
	case tok_continue: return "continue";
	case tok_break: return "break";
	case tok_identifier: return "identifier";
	case tok_string: return "string";
	case tok_number: return "number";
	case tok_question_mark: return "?";
	}

	assert(false && "unreachable");
	return "";
}

__attribute__ ((format(printf, 2, 3)))
static void
lex_error(struct lexer *l, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	struct token *last_tok = darr_get(&l->tok, l->tok.len - 1);
	error_message(l->path, last_tok->line, last_tok->col, fmt, args);
	va_end(args);
}

#define BUF_LEN 256
const char *
token_to_string(struct token *tok)
{
	static char buf[BUF_LEN + 1];
	uint32_t i;

	i = snprintf(buf, BUF_LEN, "%s", token_type_to_string(tok->type));
	if (tok->n) {
		i += snprintf(&buf[i], BUF_LEN - i, ":'%s'", tok->dat.s);
	}

	i += snprintf(&buf[i], BUF_LEN - i, " line %d, col: %d", tok->line, tok->col);

	return buf;
}

static void
advance(struct lexer *l)
{
	if (l->data[l->i] == '\n') {
		++l->line;
		l->line_start = l->i + 1;
	}

	++l->i;
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
		{ 0 },
	};

	uint32_t i;
	for (i = 0; keywords[i].name; ++i) {
		if (strlen(keywords[i].name) == token->n
		    && strncmp(token->dat.s, keywords[i].name, token->n) == 0) {
			token->type = keywords[i].type;
			return true;
		}
	}

	return false;
}

static enum lex_result
number(struct lexer *lexer, struct token *tok)
{
	tok->type = tok_number;

	uint32_t base = 10;

	if (lexer->data[lexer->i] == '0') {
		switch (lexer->data[lexer->i + 1]) {
		case 'x':
			base = 16;
			lexer->i += 2;
			break;
		case 'b':
			base = 2;
			lexer->i += 2;
			break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			base = 8;
			advance(lexer);
			break;
		default:
			tok->dat.n = 0;
			advance(lexer);
			return lex_cont;
		}
	}

	char *endptr = NULL;

	errno = 0;
	int64_t val = strtol(&lexer->data[lexer->i], &endptr, base);

	assert(endptr && endptr != &lexer->data[lexer->i]);

	if (errno == ERANGE) {
		if (val == LONG_MIN) {
			lex_error(lexer, "underflow when parsing number");
		} else if (val == LONG_MAX) {
			lex_error(lexer, "overflow when parsing number");
		}

		return lex_fail;
	}

	lexer->i += endptr - &lexer->data[lexer->i];

	tok->dat.n = val;

	return lex_cont;
}

static void
copy_into_token_data(struct lexer *lexer, struct token *tok, uint32_t start, uint32_t end)
{
	if (end == start) {
		return;
	}

	assert(end > start);

	tok->n = end - start;

	tok->dat.s = &lexer->data[start];
}

static enum lex_result
identifier(struct lexer *lexer, struct token *token)
{
	uint32_t start = lexer->i;

	while (is_valid_inside_of_identifier(lexer->data[lexer->i])) {
		advance(lexer);
	}

	copy_into_token_data(lexer, token, start, lexer->i);

	if (!keyword(lexer, token)) {
		token->type = tok_identifier;
	}

	return lex_cont;
}

static enum lex_result
string(struct lexer *lexer, struct token *token)
{
	token->type = tok_string;

	advance(lexer);

	uint32_t start = lexer->i;

	bool multiline = false;

	while (lexer->data[lexer->i] && lexer->data[lexer->i] != '\'') {
		if (lexer->data[lexer->i] == '\n') {
			if (multiline) {
			} else {
				lex_error(lexer, "newline in string");
				return lex_fail;
			}
		}
		advance(lexer);
	}

	if (!lexer->data[lexer->i]) {
		lex_error(lexer, "unmatched single quote (\')");
		return lex_fail;
	}

	copy_into_token_data(lexer, token, start, lexer->i);

	advance(lexer);
	return lex_cont;
}

static enum lex_result
lexer_tokenize_one(struct lexer *lexer)
{
	struct token *token = next_tok(lexer);

	while (is_skipchar(lexer->data[lexer->i])) {
		if (lexer->data[lexer->i] == '#') {
			while (lexer->data[lexer->i] && lexer->data[lexer->i] != '\n') {
				advance(lexer);
			}
		}
		advance(lexer);
	}

	*token = (struct token) {
		.line = lexer->line,
		.col = lexer->i - lexer->line_start + 1,
	};

	if (is_valid_start_of_identifier(lexer->data[lexer->i])) {
		return identifier(lexer, token);
	} else if (is_digit(lexer->data[lexer->i])) {
		return number(lexer, token);
	} else if (lexer->data[lexer->i] == '\'') {
		return string(lexer, token);
	} else {
		switch (lexer->data[lexer->i]) {
		case '\n':
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
				advance(lexer);
				token->type = tok_pluseq;
			} else {
				token->type = tok_plus;
			}
			break;
		case '-':
			token->type = tok_minus;
			break;
		case '=':
			if (lexer->data[lexer->i + 1] == '=') {
				advance(lexer);
				token->type = tok_eq;
			} else {
				token->type = tok_assign;
			}
			break;
		case '\0':
			token->type = tok_eof;
			return lex_done;
		default:
			LOG_W(log_lex, "unexpected character: '%c'", lexer->data[lexer->i]);
			return lex_fail;
		}

		advance(lexer);
		return lex_cont;
	}

	assert(false && "unreachable");
	return lex_fail;
skip:
	advance(lexer);
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
			lexer->data[(tok->dat.s - lexer->data) + tok->n] = 0;
			break;
		default:
			break;
		}

		/* L(log_lex, "%s", token_to_string(tok)); */
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
