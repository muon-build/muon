/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Seedo Paul <seedoeldhopaul@gmail.com>
 * SPDX-FileCopyrightText: Serge Zaitsev
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "error.h"
#include "external/tinyjson.h"
#include "lang/lexer.h"
#include "lang/object.h"
#include "lang/string.h"
#include "log.h"
#include "platform/assert.h"

/*******************************************************************************
 * below is a heavily modified version of jmsn with muon specific additons
 *
 * jmsn was replaced with json because I find that much easier to read and type
 ******************************************************************************/

#include <stddef.h>

enum json_type {
	json_type_eof,
	json_type_string,
	json_type_true,
	json_type_false,
	json_type_number,
	json_type_null,
	json_type_comma = ',',
	json_type_colon = ':',
	json_type_lbrack = '[',
	json_type_rbrack = ']',
	json_type_lcurl = '{',
	json_type_rcurl = '}',
};

static const char *json_type_str[] = {
	[json_type_eof] = "eof",
	[json_type_string] = "string",
	[json_type_true] = "true,",
	[json_type_false] = "false",
	[json_type_number] = "number",
	[json_type_null] = "null",
	[json_type_comma] = ",",
	[json_type_colon] = ":",
	[json_type_lbrack] = "[",
	[json_type_rbrack] = "]",
	[json_type_lcurl] = "{",
	[json_type_rcurl] = "}",
};

struct json_token {
	enum json_type type;
	union literal_data data;
	struct source_location loc;
};

struct json_parser {
	struct json_token tok;
	const struct str *js;
	uint32_t pos, stack_depth;
	obj err;
};

MUON_ATTR_FORMAT(printf, 3, 4)
static bool
json_error(struct workspace *wk, struct json_parser *p, const char *fmt, ...)
{
	struct source src = {
		.src = p->js->s,
		.len = p->js->len,
	};
	struct detailed_source_location dloc;
	get_detailed_source_location(&src, p->tok.loc, &dloc, 0);

	TSTR(buf);

	tstr_pushf(wk, &buf, "%d:%d: ", dloc.line, dloc.col);

	va_list ap;
	va_start(ap, fmt);
	tstr_vpushf(wk, &buf, fmt, ap);
	va_end(ap);

	p->err = tstr_into_str(wk, &buf);
	return false;
}

static bool
json_eof(struct json_parser *p)
{
	return p->pos >= p->js->len;
}

/**
 * Fills next available token with JSON primitive.
 */
static bool
json_lex_number(struct workspace *wk, struct json_parser *p)
{
	struct str s = { .s = p->js->s + p->pos, .len = 0 };
	for (; !json_eof(p); ++p->pos) {
		switch (p->js->s[p->pos]) {
		case '\t':
		case '\r':
		case '\n':
		case ' ':
		case ',':
		case ']':
		case '}': {
			goto done;
		}
		default:
			/* to quiet a warning from gcc*/
			break;
		}

		if (p->js->s[p->pos] < 32 || p->js->s[p->pos] >= 127) {
			return json_error(wk, p, "unexpected character '%c' in number", p->js->s[p->pos]);
		}
	}

done:
	s.len = p->pos - (s.s - p->js->s);
	p->tok.loc.len = s.len;

	if (!s.len) {
		goto badnum;
	}

	if (memchr(s.s, 'e', s.len) || memchr(s.s, '.', s.len)) {
		goto badreal;
	}

	int64_t sign = 1;
	if (s.s[0] == '-') {
		sign = -1;
		++s.s;
		--s.len;
	} else if (s.s[0] == '+') {
		sign = 1;
		++s.s;
		--s.len;
	}

	if (!s.len) {
		goto badnum;
	} else if (s.s[0] == '0' && s.len > 1) {
		goto badnum;
	}

	int64_t i;
	if (!str_to_i(&s, &i, false)) {
		goto badnum;
	}

	i *= sign;

	p->tok.type = json_type_number;
	p->tok.data.num = i;
	return true;
badnum:
	return json_error(wk, p, "invalid number '%.*s'", s.len, s.s);
badreal:
	return json_error(wk, p, "muon does not support reals: '%.*s'", s.len, s.s);
}

/**
 * Fills next token with JSON string.
 */
static bool
json_lex_string(struct workspace *wk, struct json_parser *p)
{
	TSTR(buf);

	/* Skip starting quote */
	p->pos++;

	for (; !json_eof(p); ++p->pos) {
		char c = p->js->s[p->pos];

		/* Quote: end of string */
		if (c == '\"') {
			++p->pos;
			p->tok.type = json_type_string;
			p->tok.data.str = tstr_into_str(wk, &buf);
			return true;
		}

		/* Backslash: Quoted symbol expected */
		if (c == '\\' && p->pos + 1 < p->js->len) {
			int32_t i;
			p->pos++;
			switch (p->js->s[p->pos]) {
			case '\"': tstr_push(wk, &buf, '\"'); break;
			case '/': tstr_push(wk, &buf, '/'); break;
			case '\\': tstr_push(wk, &buf, '\\'); break;
			case 'b': tstr_push(wk, &buf, '\b'); break;
			case 'f': tstr_push(wk, &buf, '\f'); break;
			case 'r': tstr_push(wk, &buf, '\r'); break;
			case 'n': tstr_push(wk, &buf, '\n'); break;
			case 't': tstr_push(wk, &buf, '\t'); break;
			case 'u':
				++p->pos;
				char num[5] = { 0 };
				for (i = 0; i < 4 && !json_eof(p); ++i) {
					if (!is_hex_digit(p->js->s[p->pos])) {
						return json_error(wk, p, "unterminated hex escape");
					}
					num[i] = p->js->s[p->pos];
					++p->pos;
				}
				uint32_t val = strtol(num, 0, 16);

				if (!lex_string_escape_utf8(wk, &buf, val)) {
					json_error(wk, p, "invalid utf-8 escape 0x%x", val);
				}

				p->pos--;
				break;
			default: {
				return json_error(wk, p, "invalid escape %c", p->js->s[p->pos]);
			}
			}
		} else if (c == 0) {
			return json_error(wk, p, "unescaped control character in string");
		} else if (c == '\n' || c == '\t' || c == '\r') {
			return json_error(wk, p, "unescaped whitespace in string");
		} else {
			tstr_push(wk, &buf, c);
		}
	}

	return json_error(wk, p, "eof when parsing string");
}

static bool
json_lex_identifier(struct workspace *wk, struct json_parser *p, const struct str *expected, enum json_type t)
{
	if (strncmp(&p->js->s[p->pos], expected->s, expected->len) != 0) {
		return json_error(wk, p, "expected %s", expected->s);
	}
	p->tok.type = t;
	p->pos += expected->len;
	return true;
}

/**
 * Run JSON p. It parses a JSON data string into and array of tokens, each
 * describing
 * a single JSON object.
 */
static bool
json_next_token(struct workspace *wk, struct json_parser *p, struct json_token *token)
{
	for (; is_whitespace(p->js->s[p->pos]) && !json_eof(p); ++p->pos) {
	}

	token->loc.off = p->pos;
	token->loc.len = 1;

	for (; !json_eof(p); ++p->pos) {
		char c;
		c = p->js->s[p->pos];

		switch (c) {
		case '{':
		case '}':
		case '[':
		case ']':
		case ':':
		case ',': {
			token->type = c;
			++p->pos;
			return true;
		}
		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9': return json_lex_number(wk, p);
		case '\"': return json_lex_string(wk, p);
		case 't': return json_lex_identifier(wk, p, &STR("true"), json_type_true);
		case 'f': return json_lex_identifier(wk, p, &STR("false"), json_type_false);
		case 'n': return json_lex_identifier(wk, p, &STR("null"), json_type_null);
		default: return json_error(wk, p, "unexpected character %c", c);
		}
	}

	token->type = json_type_eof;
	return true;
}

static bool
json_accept(struct workspace *wk, struct json_parser *p, enum json_type t)
{
	if (p->err) {
		return false;
	}

	if (p->tok.type == t) {
		json_next_token(wk, p, &p->tok);
		return true;
	}

	return false;
}

static bool
json_expect(struct workspace *wk, struct json_parser *p, enum json_type t)
{
	if (!json_accept(wk, p, t)) {
		if (!p->err) {
			json_error(wk, p, "expected %s, got %s", json_type_str[t], json_type_str[p->tok.type]);
		}
		return false;
	}

	return true;
}

static obj json_parse(struct workspace *wk, struct json_parser *p);

static obj
json_parse_container(struct workspace *wk, struct json_parser *p, enum json_type container_type)
{
	if (p->stack_depth > 1024) {
		json_error(wk, p, "stack too deep");
		return 0;
	}

	++p->stack_depth;

	enum json_type end_type = container_type == '{' ? '}' : ']';
	obj res = make_obj(wk, container_type == '{' ? obj_dict : obj_array);
	if (json_accept(wk, p, end_type)) {
		goto done;
	}

	do {
		obj v = json_parse(wk, p);
		if (container_type == '{') {
			if (get_obj_type(wk, v) != obj_string) {
				json_error(wk, p, "object keys must be strings");
				goto done;
			}

			obj k = v;
			if (!json_expect(wk, p, ':')) {
				goto done;
			}

			v = json_parse(wk, p);
			obj_dict_set(wk, res, k, v);
		} else {
			obj_array_push(wk, res, v);
		}
	} while (json_accept(wk, p, ','));

	json_expect(wk, p, end_type);

done:
	--p->stack_depth;
	return res;
}

obj
json_parse(struct workspace *wk, struct json_parser *p)
{
	if (p->err) {
		return 0;
	}

	switch (p->tok.type) {
	case json_type_string: {
		json_accept(wk, p, p->tok.type);
		return p->tok.data.str;
	}
	case json_type_true: {
		json_accept(wk, p, p->tok.type);
		return obj_bool_true;
	}
	case json_type_false: {
		json_accept(wk, p, p->tok.type);
		return obj_bool_false;
	}
	case json_type_null: {
		json_accept(wk, p, p->tok.type);
		return 0;
	}
	case json_type_number: {
		json_accept(wk, p, p->tok.type);
		return make_number(wk, p->tok.data.num);
	}
	case '{': {
		json_accept(wk, p, p->tok.type);
		return json_parse_container(wk, p, '{');
	}
	case '[': {
		json_accept(wk, p, p->tok.type);
		return json_parse_container(wk, p, '[');
	}
	default: {
		json_error(wk, p, "unexpected token %s", json_type_str[p->tok.type]);
		return 0;
	}
	}
}

bool
muon_json_to_obj(struct workspace *wk, const struct str *js, obj *res)
{
	struct json_parser p = {
		.js = js,
	};
	json_next_token(wk, &p, &p.tok);

	*res = json_parse(wk, &p);

	json_expect(wk, &p, json_type_eof);

	if (p.err) {
		*res = p.err;
	}
	return !p.err;
}
