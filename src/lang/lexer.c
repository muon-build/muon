/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "error.h"
#include "lang/lexer.h"
#include "platform/assert.h"

/******************************************************************************
* token printing
******************************************************************************/

const char *
token_type_to_s(enum token_type type)
{
	switch (type) {
	case token_type_error: return "error";
	case token_type_eof: return "end of file";
	case token_type_eol: return "end of line";
	case token_type_lparen: return "(";
	case token_type_rparen: return ")";
	case token_type_lbrack: return "[";
	case token_type_rbrack: return "]";
	case token_type_lcurl: return "{";
	case token_type_rcurl: return "}";
	case token_type_dot: return ".";
	case token_type_comma: return ",";
	case token_type_colon: return ":";
	case token_type_assign: return "=";
	case token_type_plus: return "+";
	case token_type_minus: return "-";
	case token_type_star: return "*";
	case token_type_slash: return "/";
	case token_type_modulo: return "%";
	case token_type_plus_assign: return "+=";
	case token_type_eq: return "==";
	case token_type_neq: return "!=";
	case token_type_gt: return ">";
	case token_type_geq: return ">=";
	case token_type_lt: return "<";
	case token_type_leq: return "<=";
	case token_type_true: return "true";
	case token_type_false: return "false";
	case token_type_if: return "if";
	case token_type_else: return "else";
	case token_type_elif: return "elif";
	case token_type_endif: return "endif";
	case token_type_and: return "and";
	case token_type_or: return "or";
	case token_type_not: return "not";
	case token_type_not_in: return "not in";
	case token_type_foreach: return "foreach";
	case token_type_endforeach: return "endforeach";
	case token_type_in: return "in";
	case token_type_continue: return "continue";
	case token_type_break: return "break";
	case token_type_identifier: return "identifier";
	case token_type_string: return "string";
	case token_type_fstring: return "fstring";
	case token_type_number: return "number";
	case token_type_question_mark: return "?";
	case token_type_func: return "func";
	case token_type_endfunc: return "endfunc";
	case token_type_return: return "return";
	case token_type_bitor: return "|";
	case token_type_returntype: return "->";
	case token_type_doc_comment: return "doc comment";
	case token_type_null: return "null";
	}

	UNREACHABLE_RETURN;
}

const char *
token_to_s(struct workspace *wk, struct token *token)
{
	assert(token);

	static char buf[BUF_SIZE_S + 1];
	uint32_t i;

	i = snprintf(buf, BUF_SIZE_S, "%s", token_type_to_s(token->type));
	if (token->type == token_type_string || token->type == token_type_fstring
		|| token->type == token_type_identifier || token->type == token_type_error) {
		i += obj_snprintf(wk, &buf[i], BUF_SIZE_S - i, ":%o", token->data.str);
	} else if (token->type == token_type_number) {
		i += snprintf(&buf[i], BUF_SIZE_S - i, ":%" PRIi64, token->data.num);
	}

	/* i += snprintf(&buf[i], BUF_SIZE_S - i, " off %d, len: %d", token->location.off, token->location.len); */

	return buf;
}

/******************************************************************************
* char types
******************************************************************************/

bool
is_valid_start_of_identifier(const char c)
{
	return c == '_' || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

static bool
is_digit(const char c)
{
	return '0' <= c && c <= '9';
}

bool
is_hex_digit(const char c)
{
	return is_digit(c) || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
}

bool
is_valid_inside_of_identifier(const char c)
{
	return is_valid_start_of_identifier(c) || is_digit(c);
}

static bool
is_skipchar(const char c)
{
	return c == '\r' || c == ' ' || c == '\t' || c == '#';
}

/******************************************************************************
* lexer utils
******************************************************************************/

#define lexer_str(__len)                                                                                             \
	(struct str)                                                                                                 \
	{                                                                                                            \
		&lexer->src[lexer->i], lexer->i + __len > lexer->source->len ? lexer->source->len - lexer->i : __len \
	}

static void
lex_advance(struct lexer *lexer)
{
	if (lexer->i >= lexer->source->len) {
		return;
	}

	++lexer->i;
}

static void
lex_advance_n(struct lexer *lexer, uint32_t n)
{
	uint32_t i;
	for (i = 0; i < n; ++i) {
		lex_advance(lexer);
	}
}

struct lex_str_token_table {
	struct str str;
	int32_t token_type;
	int32_t token_subtype;
};

static bool
lex_str_token_lookup(struct lexer *lexer,
	struct token *token,
	const struct lex_str_token_table *table,
	uint32_t table_len,
	struct str *str)
{
	uint32_t i;

	for (i = 0; i < table_len; ++i) {
		if (str_eql(&table[i].str, str)) {
			token->type = table[i].token_type;
			token->location.len = table[i].str.len;
			token->data.type = table[i].token_subtype;
			return true;
		}
	}

	return false;
}

static void
lex_copy_str(struct lexer *lexer, struct token *token, uint32_t start, uint32_t end)
{
	token->data.str = make_strn(lexer->wk, &lexer->src[start], end - start);
	token->location.len = end - start;
}

static void
MUON_ATTR_FORMAT(printf, 3, 4) lex_error_token(struct lexer *lexer, struct token *token, const char *fmt, ...)
{
	token->type = token_type_error;

	va_list args;
	va_start(args, fmt);
	token->data.str = make_strfv(lexer->wk, fmt, args);
	va_end(args);
}

/******************************************************************************
* lexer
******************************************************************************/

static const struct lex_str_token_table lex_2chr_tokens[] = {
	{ STR_static("!="), token_type_neq },
	{ STR_static("+="), token_type_plus_assign },
	{ STR_static("<="), token_type_leq },
	{ STR_static("=="), token_type_eq },
	{ STR_static(">="), token_type_geq },
};

static const struct lex_str_token_table lex_2chr_tokens_func[] = {
	{ STR_static("->"), token_type_returntype },
};

static const struct lex_str_token_table lex_keyword_tokens[] = {
	{ STR_static("and"), token_type_and },
	{ STR_static("break"), token_type_break },
	{ STR_static("continue"), token_type_continue },
	{ STR_static("elif"), token_type_elif },
	{ STR_static("else"), token_type_else },
	{ STR_static("endforeach"), token_type_endforeach },
	{ STR_static("endif"), token_type_endif },
	{ STR_static("false"), token_type_false },
	{ STR_static("foreach"), token_type_foreach },
	{ STR_static("if"), token_type_if },
	{ STR_static("in"), token_type_in },
	{ STR_static("not"), token_type_not },
	{ STR_static("or"), token_type_or },
	{ STR_static("true"), token_type_true },
};

static const struct lex_str_token_table lex_keyword_tokens_func[] = {
	{ STR_static("endfunc"), token_type_endfunc },
	{ STR_static("func"), token_type_func },
	{ STR_static("return"), token_type_return },
	{ STR_static("null"), token_type_null },
};

static void
lex_number(struct lexer *lexer, struct token *token)
{
	token->type = token_type_number;

	uint32_t base = 10;
	uint32_t start = lexer->i;

	if (lexer->src[lexer->i] == '0') {
		switch (lexer->src[lexer->i + 1]) {
		case 'X':
		case 'x':
			base = 16;
			lexer->i += 2;
			break;
		case 'B':
		case 'b':
			base = 2;
			lexer->i += 2;
			break;
		case 'O':
		case 'o':
			base = 8;
			lexer->i += 2;
			break;
		default:
			lex_advance(lexer);
			if (lexer->mode & lexer_mode_fmt) {
				lex_copy_str(lexer, token, start, lexer->i);
			} else {
				token->data.num = 0;
			}
			return;
		}
	}

	char *endptr = 0;
	errno = 0;
	int64_t val = strtoll(&lexer->src[lexer->i], &endptr, base);

	assert(endptr);
	if (endptr == &lexer->src[lexer->i]) {
		++lexer->i;
		lex_error_token(lexer, token, "invalid number");
		return;
	}

	lexer->i += endptr - &lexer->src[lexer->i];

	if (errno == ERANGE) {
		lex_error_token(lexer,
			token,
			"number out of representable range [%" PRId64 ",%" PRId64 "]",
			INT64_MIN,
			INT64_MAX);
		return;
	}

	if (lexer->mode & lexer_mode_fmt) {
		lex_copy_str(lexer, token, start, lexer->i);
	} else {
		token->data.num = val;
	}
}

bool
lex_string_escape_utf8(struct workspace *wk, struct tstr *buf, uint32_t val)
{
	uint8_t pre, b, pre_len;
	uint32_t len, i;

	/* From: https://en.wikipedia.org/wiki/UTF-8#Encoding
	 * U+0000  - U+007F   0x00 0xxxxxxx
	 * U+0080  - U+07FF   0xc0 110xxxxx 10xxxxxx
	 * U+0800  - U+FFFF   0xe0 1110xxxx 10xxxxxx 10xxxxxx
	 * U+10000 - U+10FFFF 0xf0 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
	 * */

	if (val <= 0x7f) {
		tstr_push(wk, buf, val);
		return true;
	} else if (val <= 0x07ff) {
		len = 2;
		pre_len = 5;
		pre = 0xc0;
		b = 11;
	} else if (val <= 0xffff) {
		len = 3;
		pre_len = 4;
		pre = 0xe0;
		b = 16;
	} else if (val <= 0x10ffff) {
		len = 4;
		pre_len = 3;
		pre = 0xf0;
		b = 21;
	} else {
		return false;
	}

	tstr_push(wk, buf, pre | (val >> (b - pre_len)));

	for (i = 1; i < len; ++i) {
		tstr_push(wk, buf, 0x80 | ((val >> (b - pre_len - (6 * i))) & 0x3f));
	}

	return true;
}

static bool
lex_string_escape(struct lexer *lexer, struct token *token, struct tstr *buf)
{
	switch (lexer->src[lexer->i + 1]) {
	case '\\':
	case '\'':
		lex_advance(lexer);
		tstr_push(lexer->wk, buf, lexer->src[lexer->i]);
		return true;
	case 'a':
		lex_advance(lexer);
		tstr_push(lexer->wk, buf, '\a');
		return true;
	case 'b':
		lex_advance(lexer);
		tstr_push(lexer->wk, buf, '\b');
		return true;
	case 'f':
		lex_advance(lexer);
		tstr_push(lexer->wk, buf, '\f');
		return true;
	case 'r':
		lex_advance(lexer);
		tstr_push(lexer->wk, buf, '\r');
		return true;
	case 't':
		lex_advance(lexer);
		tstr_push(lexer->wk, buf, '\t');
		return true;
	case 'v':
		lex_advance(lexer);
		tstr_push(lexer->wk, buf, '\v');
		return true;
	case 'n':
		lex_advance(lexer);
		tstr_push(lexer->wk, buf, '\n');
		return true;
	case 'x':
	case 'u':
	case 'U': {
		uint32_t len = 0;
		switch (lexer->src[lexer->i + 1]) {
		case 'x': len = 2; break;
		case 'u': len = 4; break;
		case 'U': len = 8; break;
		}
		lex_advance(lexer);

		char num[9] = { 0 };
		uint32_t i;

		for (i = 0; i < len; ++i) {
			num[i] = lexer->src[lexer->i + 1];
			if (!is_hex_digit(num[i])) {
				lex_error_token(lexer, token, "unterminated hex escape");
				return false;
			}
			lex_advance(lexer);
		}

		uint32_t val = strtol(num, 0, 16);

		if (!lex_string_escape_utf8(lexer->wk, buf, val)) {
			lex_error_token(lexer, token, "invalid utf-8 escape 0x%x", val);
			return false;
		}
		return true;
	}
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9': {
		char num[4] = { 0 };
		uint32_t i;

		for (i = 0; i < 3; ++i) {
			num[i] = lexer->src[lexer->i + 1];
			if (!is_digit(num[i])) {
				break;
			}
			lex_advance(lexer);
		}

		tstr_push(lexer->wk, buf, strtol(num, 0, 8));
		return true;
	}
	default:
		tstr_push(lexer->wk, buf, lexer->src[lexer->i]);
		lex_advance(lexer);
		tstr_push(lexer->wk, buf, lexer->src[lexer->i]);
		return true;
	case 0: lex_error_token(lexer, token, "unterminated hex escape"); return false;
	}

	UNREACHABLE_RETURN;
}

typedef bool(lex_string_escape_fun)(struct lexer *lexer, struct token *token, struct tstr *buf);

static void
lex_basic_string(struct lexer *lexer, struct token *token, struct tstr *buf, char end, lex_string_escape_fun escape)
{
	lex_advance(lexer);

	for (; lexer->i < lexer->source->len && lexer->src[lexer->i] != end; lex_advance(lexer)) {
		switch (lexer->src[lexer->i]) {
		case 0:
		case '\n': goto unterminated_string;
		case '\\': {
			if (!lex_string_escape(lexer, token, buf)) {
				return;
			}
			break;
		}
		default: tstr_push(lexer->wk, buf, lexer->src[lexer->i]); break;
		}
	}

	if (lexer->src[lexer->i] != end) {
unterminated_string:
		lex_error_token(lexer, token, "unterminated string");
		return;
	}

	lex_advance(lexer);

	token->data.str = tstr_into_str(lexer->wk, buf);
}

static void
lex_string(struct lexer *lexer, struct token *token)
{
	const struct str multiline_terminator = STR("'''");
	TSTR(buf);

	if (str_eql(&lexer_str(multiline_terminator.len), &multiline_terminator)) {
		lex_advance_n(lexer, multiline_terminator.len);

		while (lexer->source->len - lexer->i >= multiline_terminator.len
			&& !str_eql(&lexer_str(multiline_terminator.len), &multiline_terminator)) {
			if (lexer->src[lexer->i] != '\r') {
				tstr_push(lexer->wk, &buf, lexer->src[lexer->i]);
			}
			lex_advance(lexer);
		}

		if (str_eql(&lexer_str(multiline_terminator.len), &multiline_terminator)) {
			lex_advance_n(lexer, 3);
			token->data.str = tstr_into_str(lexer->wk, &buf);
		} else {
			lex_error_token(lexer, token, "unterminated multiline string");
		}

		return;
	}

	lex_basic_string(lexer, token, &buf, '\'', lex_string_escape);
}

enum lexer_enclosed_state {
	lexer_enclosed_state_none = 0,
	lexer_enclosed_state_enclosed = 1,
};

static void
lexer_push_pop_enclosed_state(struct lexer *lexer, enum token_type type)
{
	bool pop = false;
	uint8_t enclosed_state;

	switch (type) {
	case token_type_func: {
		enclosed_state = lexer_enclosed_state_none;
		break;
	}
	case token_type_endfunc: {
		pop = true;
		break;
	}
	case '(':
	case '[':
	case '{': {
		enclosed_state = lexer_enclosed_state_enclosed;
		break;
	}
	case ')':
	case ']':
	case '}': {
		pop = true;
		break;
	}
	default: return;
	}

	if (pop) {
		if (lexer->stack.len) {
			stack_pop(&lexer->stack, lexer->enclosed_state);
		}
	} else {
		stack_push(&lexer->stack, lexer->enclosed_state, enclosed_state);
	}
}

void
lexer_next(struct lexer *lexer, struct token *token)
{
	uint32_t start;
	lexer->ws_start = lexer->i, lexer->ws_end = lexer->i;
restart:
	*token = (struct token){
		.location = (struct source_location){ .off = lexer->i, .len = 1 },
	};

	if (lexer->i >= lexer->source->len) {
		token->type = token_type_eof;
		return;
	}

	if (lexer->mode & lexer_mode_bom_error) {
		lex_error_token(lexer, token, "Unexpected utf-8 BOM.  File must be in utf-8 with no BOM.");
		lexer->mode &= ~lexer_mode_bom_error;
		lexer->i += 3;
		return;
	}

	while (is_skipchar(lexer->src[lexer->i])) {
		if (lexer->src[lexer->i] == '#') {
			lex_advance(lexer);

			obj doc_comment = 0;
			if (!(lexer->mode & lexer_mode_fmt) && (lexer->mode & lexer_mode_functions)
				&& lexer->src[lexer->i] == '#') {
				lex_advance(lexer);
				if (strchr(" \t", lexer->src[lexer->i])) {
					lex_advance(lexer);
				}
				doc_comment = make_str(lexer->wk, "");
			}

			start = lexer->i;

			while (lexer->src[lexer->i]) {
				if (doc_comment) {
					if (lexer->src[lexer->i] == '\n') {
						uint32_t skip = 1;
						while (strchr(" \t", lexer->src[lexer->i + skip])) {
							++skip;
						}

						if (str_startswith(&STRL(&lexer->src[lexer->i + skip]), &STR("##"))) {
							skip += 2;

							uint32_t i;
							for (i = 0; i < skip; ++i) {
								lex_advance(lexer);
							}

							str_app(lexer->wk, &doc_comment, "\n");

							if (strchr(" \t", lexer->src[lexer->i])) {
								lex_advance(lexer);
							}
							continue;
						} else {
							break;
						}
					} else {
						str_appn(lexer->wk, &doc_comment, &lexer->src[lexer->i], 1);
					}
				} else if (lexer->src[lexer->i] == '\n') {
					break;
				}

				lex_advance(lexer);
			}

			if (lexer->mode & lexer_mode_fmt) {
				bool fmt_on;
				obj s;

				s = make_strn(lexer->wk, &lexer->src[start], lexer->i - start);
				s = str_strip(lexer->wk, get_str(lexer->wk, s), 0, 0);
				if (lexer_is_fmt_comment(get_str(lexer->wk, s), &fmt_on)) {
					if (fmt_on) {
						if (lexer->fmt.in_raw_block) {
							s = make_strn(lexer->wk,
								&lexer->src[lexer->fmt.raw_block_start],
								(start - 1) - lexer->fmt.raw_block_start);

							obj_array_push(lexer->wk, lexer->fmt.raw_blocks, s);
							lexer->fmt.in_raw_block = false;
						}
					} else {
						if (!lexer->fmt.in_raw_block) {
							lexer->fmt.raw_block_start = lexer->i;
							lexer->fmt.in_raw_block = true;
						}
					}
				}
			} else if (doc_comment) {
				token->location.off = start;
				token->type = token_type_doc_comment;
				token->data.str = doc_comment;
				return;
			}
		} else {
			lex_advance(lexer);
		}
	}

	if (str_eql(&lexer_str(2), &STR("\\\n"))) {
		lex_advance_n(lexer, 2);
		goto restart;
	} else if (str_eql(&lexer_str(3), &STR("\\\r\n"))) {
		lex_advance_n(lexer, 3);
		goto restart;
	}

	lexer->ws_end = lexer->i;
	token->location.off = lexer->i;

	struct str lexer_str_2chr = lexer_str(2);
	if (lex_str_token_lookup(lexer, token, lex_2chr_tokens, ARRAY_LEN(lex_2chr_tokens), &lexer_str_2chr)
		|| ((lexer->mode & lexer_mode_functions)
			&& lex_str_token_lookup(
				lexer, token, lex_2chr_tokens_func, ARRAY_LEN(lex_2chr_tokens_func), &lexer_str_2chr))) {
		lex_advance_n(lexer, 2);
		return;
	}

	if (str_eql(&lexer_str(2), &STR("f\'"))) {
		start = lexer->i;
		lex_advance(lexer);
		token->type = token_type_fstring;
		lex_string(lexer, token);
		token->location.len = lexer->i - token->location.off;
		if (lexer->mode & lexer_mode_fmt && token->type != token_type_error) {
			token->type = token_type_string;
			lex_copy_str(lexer, token, start, lexer->i);
		}
		return;
	} else if (is_valid_start_of_identifier(lexer->src[lexer->i])) {
		start = lexer->i;
		struct str str = { &lexer->src[lexer->i] };

		while (is_valid_inside_of_identifier(lexer->src[lexer->i])) {
			lex_advance(lexer);
			++str.len;
		}

		if (lex_str_token_lookup(lexer, token, lex_keyword_tokens, ARRAY_LEN(lex_keyword_tokens), &str)
			|| ((lexer->mode & lexer_mode_functions)
				&& lex_str_token_lookup(lexer,
					token,
					lex_keyword_tokens_func,
					ARRAY_LEN(lex_keyword_tokens_func),
					&str))) {
			lexer_push_pop_enclosed_state(lexer, token->type);
			return;
		} else {
			token->type = token_type_identifier;
			lex_copy_str(lexer, token, start, lexer->i);
		}

		return;
	} else if (is_digit(lexer->src[lexer->i])) {
		lex_number(lexer, token);
		token->location.len = lexer->i - token->location.off;
		return;
	}

	switch (lexer->src[lexer->i]) {
	case '\n':
		lex_advance(lexer);

		if (lexer->enclosed_state) {
			goto restart;
		} else {
			token->type = token_type_eol;
		}
		return;
	case '\'':
		start = lexer->i;
		token->type = token_type_string;
		lex_string(lexer, token);
		token->location.len = lexer->i - token->location.off;
		if (lexer->mode & lexer_mode_fmt && token->type != token_type_error) {
			lex_copy_str(lexer, token, start, lexer->i);
		}
		return;
	case '(':
	case '[':
	case '{':
		token->type = lexer->src[lexer->i];
		lexer_push_pop_enclosed_state(lexer, token->type);
		break;
	case ')':
	case ']':
	case '}':
		token->type = lexer->src[lexer->i];
		lexer_push_pop_enclosed_state(lexer, token->type);
		break;
	case '.':
	case ',':
	case ':':
	case '?':
	case '+':
	case '-':
	case '*':
	case '/':
	case '%':
	case '=':
	case '>':
	case '<': token->type = lexer->src[lexer->i]; break;
	case '|':
		if (!(lexer->mode & lexer_mode_functions)) {
			goto unexpected_character;
		}

		token->type = lexer->src[lexer->i];
		break;
	case '\0':
		if (lexer->i != lexer->source->len) {
			goto unexpected_character;
		}

		token->type = token_type_eof;
		break;
	default:
unexpected_character:
		lex_error_token(lexer, token, "unexpected character: '%c'", lexer->src[lexer->i]);
		break;
	}

	lex_advance(lexer);
	return;
}

void
lexer_init(struct lexer *lexer, struct workspace *wk, const struct source *src, enum lexer_mode mode)
{
	*lexer = (struct lexer){
		.wk = wk,
		.source = src,
		.src = src->src,
		.mode = mode,
	};

	if (src->len >= 3 && memcmp(src->src, (uint8_t []){ 0xef, 0xbb, 0xbf }, 3) == 0) {
		lexer->mode |= lexer_mode_bom_error;
	}

	stack_init(&lexer->stack, 2048);

	if (lexer->mode & lexer_mode_fmt) {
		lexer->fmt.raw_blocks = make_obj(lexer->wk, obj_array);
	}
}

void
lexer_destroy(struct lexer *lexer)
{
	stack_destroy(&lexer->stack);
}

/******************************************************************************
* fmt related
******************************************************************************/

obj
lexer_get_preceeding_whitespace(struct lexer *lexer)
{
	return make_strn(lexer->wk, &lexer->src[lexer->ws_start], lexer->ws_end - lexer->ws_start);
}

bool
lexer_is_fmt_comment(const struct str *comment, bool *fmt_on)
{
	if (str_eql(comment, &STR("fmt:off")) || str_eql(comment, &STR("fmt: off"))) {
		*fmt_on = false;
		return true;
	} else if (str_eql(comment, &STR("fmt:on")) || str_eql(comment, &STR("fmt: on"))) {
		*fmt_on = true;
		return true;
	}

	return false;
}

/******************************************************************************
* cmake
******************************************************************************/

static const struct lex_str_token_table cm_lex_keyword_tokens_command[] = {
	{ STR_static("else"), token_type_else },
	{ STR_static("elseif"), token_type_elif },
	{ STR_static("endif"), token_type_endif },
	{ STR_static("if"), token_type_if },
};

static const struct lex_str_token_table cm_lex_keyword_tokens_conditional[] = {
	{ STR_static("NOT"), token_type_not },
	{ STR_static("AND"), token_type_and },
	{ STR_static("OR"), token_type_or },
	{ STR_static("EQUAL"), token_type_eq },
	{ STR_static("LESS"), '<' },
	{ STR_static("LESS_EQUAL"), token_type_leq },
	{ STR_static("GREATER"), '>' },
	{ STR_static("GREATER_EQUAL"), token_type_geq },
	{ STR_static("STR_EQUAL"), token_type_eq, cm_token_subtype_comp_str },
	{ STR_static("STR_LESS"), '<', cm_token_subtype_comp_str },
	{ STR_static("STR_LESS_EQUAL"), token_type_leq, cm_token_subtype_comp_str },
	{ STR_static("STR_GREATER"), '>', cm_token_subtype_comp_str },
	{ STR_static("STR_GREATER_EQUAL"), token_type_geq, cm_token_subtype_comp_str },
	{ STR_static("VERSION_EQUAL"), token_type_eq, cm_token_subtype_comp_ver },
	{ STR_static("VERSION_LESS"), '<', cm_token_subtype_comp_ver },
	{ STR_static("VERSION_LESS_EQUAL"), token_type_leq, cm_token_subtype_comp_ver },
	{ STR_static("VERSION_GREATER"), '>', cm_token_subtype_comp_ver },
	{ STR_static("VERSION_GREATER_EQUAL"), token_type_geq, cm_token_subtype_comp_ver },
	{ STR_static("PATH_EQUAL"), token_type_eq, cm_token_subtype_comp_path },
	{ STR_static("MATCHES"), token_type_eq, cm_token_subtype_comp_regex },
};

void
cm_lexer_next(struct lexer *lexer, struct token *token)
{
	uint32_t start;

restart:
	*token = (struct token){
		.location = (struct source_location){ .off = lexer->i, .len = 1 },
	};

	if (lexer->i >= lexer->source->len) {
		token->type = token_type_eof;
		return;
	}

	while (is_skipchar(lexer->src[lexer->i])) {
		if (lexer->src[lexer->i] == '#') {
			lex_advance(lexer);

			start = lexer->i;

			while (lexer->src[lexer->i] && lexer->src[lexer->i] != '\n') {
				lex_advance(lexer);
			}

			if (lexer->mode & lexer_mode_fmt) {
				bool fmt_on;
				obj s;

				s = make_strn(lexer->wk, &lexer->src[start], lexer->i - start);
				s = str_strip(lexer->wk, get_str(lexer->wk, s), 0, 0);
				if (lexer_is_fmt_comment(get_str(lexer->wk, s), &fmt_on)) {
					if (fmt_on) {
						if (lexer->fmt.in_raw_block) {
							s = make_strn(lexer->wk,
								&lexer->src[lexer->fmt.raw_block_start],
								(start - 1) - lexer->fmt.raw_block_start);

							obj_array_push(lexer->wk, lexer->fmt.raw_blocks, s);
							lexer->fmt.in_raw_block = false;
						}
					} else {
						if (!lexer->fmt.in_raw_block) {
							lexer->fmt.raw_block_start = lexer->i;
							lexer->fmt.in_raw_block = true;
						}
					}
				}
			}
		} else {
			lex_advance(lexer);
		}
	}

	token->location.off = lexer->i;

	/* if (is_valid_start_of_identifier(lexer->src[lexer->i])) { */
	/* 	start = lexer->i; */
	/* 	struct str str = { &lexer->src[lexer->i] }; */

	/* 	while (is_valid_inside_of_identifier(lexer->src[lexer->i])) { */
	/* 		lex_advance(lexer); */
	/* 		++str.len; */
	/* 	} */

	/* 	token->type = token_type_identifier; */
	/* 	lex_copy_str(lexer, token, start, lexer->i); */
	/* 	return; */
	/* } */

	if (!strchr("()#\" \r\n\t", lexer->src[lexer->i])) {
		start = lexer->i;
		struct str str = { &lexer->src[lexer->i] };

		while (!strchr("()#\" \r\n\t", lexer->src[lexer->i])) {
			lex_advance(lexer);
			++str.len;
		}

		token->type = token_type_string;

		if (is_valid_start_of_identifier(lexer->src[start])) {
			uint32_t i;
			for (i = 1; i < str.len; ++i) {
				if (!is_valid_inside_of_identifier(lexer->src[start + i])) {
					break;
				}
			}

			if (i == str.len) {
				token->type = token_type_identifier;
			}
		}

		if (token->type == token_type_identifier) {
			const struct lex_str_token_table *token_table = 0;
			uint32_t token_table_len = 0;

			switch (lexer->cm_mode) {
			case cm_lexer_mode_default: break;
			case cm_lexer_mode_command:
				token_table = cm_lex_keyword_tokens_command;
				token_table_len = ARRAY_LEN(cm_lex_keyword_tokens_command);
				break;
			case cm_lexer_mode_conditional:
				token_table = cm_lex_keyword_tokens_conditional;
				token_table_len = ARRAY_LEN(cm_lex_keyword_tokens_conditional);
				break;
			}

			if (token_table && lex_str_token_lookup(lexer, token, token_table, token_table_len, &str)) {
				return;
			}
		}

		lex_copy_str(lexer, token, start, lexer->i);
		return;
	}

	switch (lexer->src[lexer->i]) {
	case '\n':
		lex_advance(lexer);

		if (lexer->enclosed_state) {
			goto restart;
		} else {
			token->type = token_type_eol;
		}
		return;
	case '"': {
		start = lexer->i;
		token->type = token_type_string;

		TSTR(buf);
		lex_basic_string(lexer, token, &buf, '"', lex_string_escape);
		token->location.len = lexer->i - token->location.off;
		return;
	}
	case '(':
		token->type = lexer->src[lexer->i];
		lexer_push_pop_enclosed_state(lexer, token->type);
		break;
	case ')':
		token->type = lexer->src[lexer->i];
		lexer_push_pop_enclosed_state(lexer, token->type);
		break;
	case '\0':
		if (lexer->i != lexer->source->len) {
			goto unexpected_character;
		}

		token->type = token_type_eof;
		break;
	default:
unexpected_character:
		lex_error_token(lexer, token, "unexpected character: '%c'", lexer->src[lexer->i]);
		break;
	}

	lex_advance(lexer);
	return;
}
