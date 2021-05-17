#include "lexer.h"
#include "log.h"
#include "mem.h"
#include "token.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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

	if (tok->n > TOKEN_MAX_DATA) {
		LOG_W(log_lex, "data too long for token");
	}

	memcpy(tok->data, &lexer->data[start], tok->n);
	tok->data[tok->n] = 0;
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

	token->data[0] = 0;
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
	while (true) {
		switch (lexer_tokenize_one(lexer)) {
		case lex_cont:
			break;
		case lex_fail:
			return false;
		case lex_done:
			return true;
		}
	}

	assert(false && "unreachable");
	return false;
}

bool
lexer_init(struct lexer *lexer, const char *path)
{
	FILE *file;

	*lexer = (struct lexer) {
		.path = path,
		.line = 1,
	};

	int64_t size, read;

	if (!(file = fopen(path, "r"))) {
		LOG_W(log_lex, "Failed to open '%s': %s", path, strerror(errno));
		return false;
	} else if (fseek(file, 0, SEEK_END) == -1) {
		LOG_W(log_lex, "Failed fseek '%s': %s", path, strerror(errno));
		return false;
	} else if ((size = ftell(file)) == -1) {
		LOG_W(log_lex, "Failed ftell '%s': %s", path, strerror(errno));
		return false;
	}

	rewind(file);
	lexer->data = z_calloc(size + 1, 1);
	lexer->data_len = size;

	darr_init(&lexer->tok, sizeof(struct token));

	read = fread(lexer->data, 1, size, file);

	if (fclose(file) != 0) {
		LOG_W(log_lex, "Failed fclose '%s': %s", path, strerror(errno));
		goto free_err;
	} else if (read != size) {
		LOG_W(log_lex, "Failed fread '%s'", path);
		goto free_err;
	}

	return true;

free_err:
	lexer_finish(lexer);
	return false;
}

void
lexer_finish(struct lexer *lexer)
{
	z_free(lexer->data);
	darr_destroy(&lexer->tok);
}
