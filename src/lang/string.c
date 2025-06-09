/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "error.h"
#include "lang/object.h"
#include "lang/object_iterators.h"
#include "lang/string.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "memmem.h"
#include "platform/assert.h"
#include "platform/mem.h"

void
str_escape(struct workspace *wk, struct tstr *sb, const struct str *ss, bool escape_printable)
{
	bool esc;
	uint32_t i;

	for (i = 0; i < ss->len; ++i) {
		esc = ss->s[i] < 32 || ss->s[i] == '\'' || ss->s[i] == '\\';
		if (!escape_printable && strchr("\t\n\r'", ss->s[i])) {
			esc = false;
		}

		if (esc) {
			if (ss->s[i] == '\'') {
				tstr_pushf(wk, sb, "\\'");
			} else if (ss->s[i] == '\\') {
				tstr_pushf(wk, sb, "\\\\");
			} else if (7 <= ss->s[i] && ss->s[i] <= 13) {
				tstr_pushf(wk, sb, "\\%c", "abtnvfr"[ss->s[i] - 7]);
			} else {
				tstr_pushf(wk, sb, "\\%d", ss->s[i]);
			}
		} else {
			tstr_push(wk, sb, ss->s[i]);
		}
	}
}

void
str_escape_json(struct workspace *wk, struct tstr *sb, const struct str *ss)
{
	uint32_t i;
	const char *esc = "\"\\";

	for (i = 0; i < ss->len; ++i) {
		if (strchr(esc, ss->s[i])) {
			tstr_pushf(wk, sb, "\\\"");
		} else if (ss->s[i] < 32 || ss->s[i] > 126) {
			if (8 <= ss->s[i] && ss->s[i] <= 13 && ss->s[i] != 11) {
				tstr_pushf(wk, sb, "\\%c", "btn_fr"[ss->s[i] - 8]);
			} else {
				tstr_pushf(wk, sb, "\\u%04x", ss->s[i]);
			}
		} else {
			tstr_push(wk, sb, ss->s[i]);
		}
	}
}

bool
str_has_null(const struct str *ss)
{
	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if (!ss->s[i]) {
			return true;
		}
	}

	return false;
}

const char *
get_cstr(struct workspace *wk, obj s)
{
	if (!s) {
		return NULL;
	}

	const struct str *ss = get_str(wk, s);

	if (str_has_null(ss)) {
		error_unrecoverable("cstr can not contain null bytes");
	}

	return ss->s;
}

static struct str *
reserve_str(struct workspace *wk, obj *s, uint32_t len)
{
	enum str_flags f = 0;
	const char *p;

	uint32_t new_len = len + 1;

	if (new_len > wk->vm.objects.chrs.bucket_size) {
		f |= str_flag_big;
		p = z_calloc(new_len, 1);
	} else {
		p = bucket_arr_pushn(&wk->vm.objects.chrs, NULL, 0, new_len);
	}

	*s = make_obj(wk, obj_string);

	struct str *str = (struct str *)get_str(wk, *s);

	*str = (struct str){
		.s = p,
		.len = len,
		.flags = f,
	};

	return str;
}

static struct str *
grow_str(struct workspace *wk, obj *s, uint32_t grow_by, bool alloc_nul)
{
	assert(s);

	struct str *ss = (struct str *)get_str(wk, *s);

	uint32_t new_len = ss->len + grow_by;

	if (!(ss->flags & str_flag_mutable)) {
		struct str *newstr = reserve_str(wk, s, new_len);
		newstr->flags |= str_flag_mutable;
		newstr->len = ss->len;
		memcpy((void *)newstr->s, ss->s, ss->len);
		return newstr;
	}

	if (alloc_nul) {
		new_len += 1;
	}

	if (ss->flags & str_flag_big) {
		ss->s = z_realloc((void *)ss->s, new_len);
		memset((void *)&ss->s[ss->len], 0, new_len - ss->len);
	} else if (new_len >= wk->vm.objects.chrs.bucket_size) {
		ss->flags |= str_flag_big;
		char *np = z_calloc(new_len, 1);
		memcpy(np, ss->s, ss->len);
		ss->s = np;
	} else {
		char *np = bucket_arr_pushn(&wk->vm.objects.chrs, ss->s, ss->len, new_len);
		ss->s = np;
	}

	return ss;
}

#define SMALL_STR_LEN 64

static obj
_make_str(struct workspace *wk, const char *p, uint32_t len, enum str_flags flags, bool hash)
{
	obj s;

	if (!p) {
		return 0;
	}

	uint64_t *v;
	if (hash && len <= SMALL_STR_LEN && (v = hash_get_strn(&wk->vm.objects.str_hash, p, len))) {
		s = *v;
		return s;
	}

	struct str *str = reserve_str(wk, &s, len);
	memcpy((void *)str->s, p, len);
	str->flags |= flags;

	if (hash && !wk->vm.objects.obj_clear_mark_set && len <= SMALL_STR_LEN) {
		hash_set_strn(&wk->vm.objects.str_hash, str->s, str->len, s);
	}
	return s;
}

bool
str_enum_add_type(struct workspace *wk, uint32_t id, obj *res)
{
	if (!obj_dict_geti(wk, wk->vm.objects.enums.types, id, res)) {
		*res = make_obj(wk, obj_dict);
		obj_dict_set(wk, *res, make_str(wk, ""), make_obj(wk, obj_array));
		obj_dict_seti(wk, wk->vm.objects.enums.types, id, *res);
		return true;
	}
	return false;
}

void
str_enum_add_type_value(struct workspace *wk, obj type, const char *value)
{
	obj values;
	if (!obj_dict_index_str(wk, type, "", &values)) {
		UNREACHABLE;
	}

	obj v = make_str_enum(wk, value, values);
	obj_array_push(wk, values, v);
	obj_dict_set(wk, type, v, v);
}

obj
str_enum_get(struct workspace *wk, obj type, const char *name)
{
	obj res;
	if (!obj_dict_index_str(wk, type, name, &res)) {
		UNREACHABLE;
	}

	return res;
}

obj
mark_typeinfo_as_enum(struct workspace *wk, obj ti, obj values)
{
	obj_dict_seti(wk, wk->vm.objects.enums.values, ti, values);

	return ti;
}

obj
make_strn_enum(struct workspace *wk, const char *str, uint32_t n, obj values)
{
	obj s = _make_str(wk, str, n, 0, false);

	obj_dict_seti(wk, wk->vm.objects.enums.values, s, values);

	return s;
}

obj
make_str_enum(struct workspace *wk, const char *str, obj values)
{
	return make_strn_enum(wk, str, strlen(str), values);
}

bool
check_str_enum(struct workspace *wk, obj l, enum obj_type l_t, obj r, enum obj_type r_t)
{
	// Only run this check in the analyzer?
	if (!wk->vm.in_analyzer) {
		return true;
	}

	enum obj_type c_t;
	obj values = 0, c = 0;

	if (l_t == obj_typeinfo) {
		type_tag t = get_obj_typeinfo(wk, l)->type;
		if (!(t & TYPE_TAG_COMPLEX)) {
			return true;
		}

		if (COMPLEX_TYPE_TYPE(t) == complex_type_preset) {
			t = complex_type_preset_get(wk, COMPLEX_TYPE_INDEX(t));
		}

		if (COMPLEX_TYPE_TYPE(t) != complex_type_enum) {
			return true;
		}

		c = r;
		c_t = r_t;
		values = COMPLEX_TYPE_INDEX(t);
	} else if (obj_dict_geti(wk, wk->vm.objects.enums.values, l, &values)) {
		c = r;
		c_t = r_t;
	} else if (r_t == obj_string && obj_dict_geti(wk, wk->vm.objects.enums.values, r, &values)) {
		c = l;
		c_t = obj_string;
	} else {
		return true;
	}

	if (c_t == obj_string) {
		if (!obj_array_in(wk, values, c)) {
			vm_warning(wk, "%o is not one of %o", c, values);
			return false;
		}
	} else if (c_t == obj_array) {
		obj v;
		obj_array_for(wk, c, v) {
			if (!obj_array_in(wk, values, v)) {
				vm_warning(wk, "%o is not one of %o", v, values);
				return false;
			}
		}
	} else if (c_t == obj_dict) {
		obj k, _v;
		obj_dict_for(wk, c, k, _v) {
			(void)_v;
			if (!obj_array_in(wk, values, k)) {
				vm_warning(wk, "%o is not one of %o", k, values);
				return false;
			}
		}
	}

	return true;
}

obj
make_strn(struct workspace *wk, const char *str, uint32_t n)
{
	return _make_str(wk, str, n, 0, true);
}

obj
make_str(struct workspace *wk, const char *str)
{
	return _make_str(wk, str, strlen(str), 0, true);
}

obj
make_strfv(struct workspace *wk, const char *fmt, va_list args)
{
	uint32_t len;
	va_list args_copy;

	va_copy(args_copy, args);
	len = vsnprintf(NULL, 0, fmt, args_copy);
	va_end(args_copy);

	obj s;
	struct str *ss = reserve_str(wk, &s, len);
	// TODO: the buffer size is too small here because the object expansion
	// isn't taken in to account by vsnprintf above.  Need to make it
	// possible to pass NULL to obj_vsnprintf to get a reliable buffer
	// length
	/* obj_vsnprintf(wk, (char *)ss->s, len + 1, fmt, args); */
	vsnprintf((char *)ss->s, len + 1, fmt, args);

	return s;
}

obj
make_strf(struct workspace *wk, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	obj s = make_strfv(wk, fmt, args);
	va_end(args);

	return s;
}

void
str_appn(struct workspace *wk, obj *s, const char *str, uint32_t n)
{
	struct str *ss = grow_str(wk, s, n, true);
	memcpy((char *)&ss->s[ss->len], str, n);
	ss->len += n;
}

void
str_apps(struct workspace *wk, obj *s, obj s_id)
{
	const struct str *str = get_str(wk, s_id);
	str_appn(wk, s, str->s, str->len);
}

void
str_app(struct workspace *wk, obj *s, const char *str)
{
	str_appn(wk, s, str, strlen(str));
}

void
str_appf(struct workspace *wk, obj *s, const char *fmt, ...)
{
	uint32_t len;
	va_list args, args_copy;
	va_start(args, fmt);
	va_copy(args_copy, args);

	len = vsnprintf(NULL, 0, fmt, args_copy);

	uint32_t olen = get_str(wk, *s)->len;
	struct str *ss = grow_str(wk, s, len, true);

	/* obj_vsnprintf(wk, (char *)ss->s, len + 1, fmt, args); */
	vsnprintf((char *)&ss->s[olen], len + 1, fmt, args);
	ss->len += len;

	va_end(args_copy);
	va_end(args);
}

obj
str_clone_mutable(struct workspace *wk, obj val)
{
	const struct str *ss = get_str(wk, val);
	return _make_str(wk, ss->s, ss->len, str_flag_mutable, false);
}

obj
str_clone(struct workspace *wk_src, struct workspace *wk_dest, obj val)
{
	const struct str *ss = get_str(wk_src, val);
	return _make_str(wk_dest, ss->s, ss->len, 0, true);
}

bool
str_eql(const struct str *ss1, const struct str *ss2)
{
	return ss1->len == ss2->len && memcmp(ss1->s, ss2->s, ss1->len) == 0;
}

static bool
str_eql_glob_impl(const struct str *ss1, const struct str *ss2, uint32_t *match_len, bool top)
{
	if (!ss1->len && !top) {
		*match_len = ss2->len;
		return true;
	}

	if (!ss2->len) {
		*match_len = 0;
		return true;
	}

	*match_len = 0;
	uint32_t i1 = 0, i2 = 0;
	for (i1 = 0; i1 < ss1->len; ++i1) {
		if (ss1->s[i1] == '*') {
			struct str sub1 = { .s = ss1->s + (i1 + 1), .len = ss1->len - (i1 + 1) },
				   sub2 = { .s = ss2->s + i2, .len = ss2->len - i2 };

			uint32_t sub_match_len, sub_match_consumed = 0;
			while (!str_eql_glob_impl(&sub1, &sub2, &sub_match_len, false)) {
				++i2;
				++sub_match_consumed;
				sub2 = (struct str){ .s = ss2->s + i2, .len = ss2->len - i2 };
			}

			if (sub_match_len) {
				sub_match_len += sub_match_consumed;
			}

			*match_len += sub_match_len;
			i1 += sub_match_len;
		} else if (ss1->s[i1] == ss2->s[i2]) {
			++*match_len;
			++i2;
		} else {
			return false;
		}
	}

	return true;
}

bool
str_eql_glob(const struct str *ss1, const struct str *ss2)
{
	uint32_t match_len;
	return str_eql_glob_impl(ss1, ss2, &match_len, true) && match_len == ss2->len;
}

static uint8_t
str_char_to_lower(uint8_t c)
{
	if ('A' <= c && c <= 'Z') {
		return c + 32;
	}

	return c;
}

void
str_to_lower(struct str *str)
{
	uint32_t i;
	char *s = (char *)str->s;
	for (i = 0; i < str->len; ++i) {
		s[i] = str_char_to_lower(str->s[i]);
	}
}

bool
str_eqli(const struct str *ss1, const struct str *ss2)
{
	if (ss1->len != ss2->len) {
		return false;
	}

	uint32_t i;
	for (i = 0; i < ss1->len; ++i) {
		if (str_char_to_lower(ss1->s[i]) != str_char_to_lower(ss2->s[i])) {
			return false;
		}
	}
	return true;
}

bool
str_startswith(const struct str *ss, const struct str *pre)
{
	if (ss->len < pre->len) {
		return false;
	}

	return memcmp(ss->s, pre->s, pre->len) == 0;
}

bool
str_startswithi(const struct str *ss, const struct str *pre)
{
	if (ss->len < pre->len) {
		return false;
	}

	uint32_t i;
	for (i = 0; i < pre->len; ++i) {
		if (str_char_to_lower(ss->s[i]) != str_char_to_lower(pre->s[i])) {
			return false;
		}
	}
	return true;
}

bool
str_endswith(const struct str *ss, const struct str *suf)
{
	if (ss->len < suf->len) {
		return false;
	}

	return memcmp(&ss->s[ss->len - suf->len], suf->s, suf->len) == 0;
}

bool
str_endswithi(const struct str *ss, const struct str *suf)
{
	if (ss->len < suf->len) {
		return false;
	}

	uint32_t i;
	for (i = 0; i < suf->len; ++i) {
		if (str_char_to_lower(ss->s[ss->len - i - 1]) != str_char_to_lower(suf->s[suf->len - i - 1])) {
			return false;
		}
	}
	return true;
}

bool
str_contains(const struct str *str, const struct str *substr)
{
	return !!memmem(str->s, str->len, substr->s, substr->len);
}

bool
str_containsi(const struct str *str, const struct str *substr)
{
	if (substr->len > str->len) {
		return false;
	} else if (substr->len == str->len) {
		return str_eqli(str, substr);
	}

	uint32_t i;
	for (i = 0; i < str->len - substr->len; ++i) {
		struct str a = { .s = str->s + i, .len = substr->len };
		if (str_eqli(&a, substr)) {
			return true;
		}
	}

	return false;
}

obj
str_join(struct workspace *wk, obj s1, obj s2)
{
	obj res;
	const struct str *ss1 = get_str(wk, s1), *ss2 = get_str(wk, s2);

	struct str *ss = reserve_str(wk, &res, ss1->len + ss2->len);

	memcpy((char *)ss->s, ss1->s, ss1->len);
	memcpy((char *)&ss->s[ss1->len], ss2->s, ss2->len);

	return res;
}

bool
is_whitespace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool
is_whitespace_except_newline(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

bool
str_to_i_base(const struct str *ss, int64_t *res, bool strip, uint32_t base)
{
	char *endptr = NULL;
	const char *start = ss->s;

	// HACK: casting away const-ness
	char *end = (char *)ss->s + ss->len;

	if (strip) {
		while (is_whitespace(*start)) {
			++start;
		}
	}

	// We need ss to be null terminated.  For example given the string
	// "this%20file" and a ss pointing at the 20 in the middle, we want to be
	// able to convert that 20 alone and not have the trailing f count as a 3rd
	// digit (hexadecimal).
	char old_end = *end;
	*end = 0;
	*res = strtoll(start, &endptr, base);
	*end = old_end;

	if (strip) {
		while (is_whitespace(*endptr)) {
			++endptr;
		}
	}

	if ((uint32_t)(endptr - ss->s) != ss->len) {
		return false;
	}

	return true;
}

bool
str_to_i(const struct str *ss, int64_t *res, bool strip)
{
	return str_to_i_base(ss, res, strip, 10);
}

obj
str_split(struct workspace *wk, const struct str *ss, const struct str *split)
{
	obj res;
	res = make_obj(wk, obj_array);

	uint32_t i, start = 0;
	obj s;

	for (i = 0; i < ss->len; ++i) {
		if (split) {
			struct str slice = { .s = &ss->s[i], .len = ss->len - i };

			if (str_startswith(&slice, split)) {
				s = make_strn(wk, &ss->s[start], i - start);

				obj_array_push(wk, res, s);

				start = i + split->len;
				i += split->len - 1;
			}
		} else {
			start = i;
			while (start < ss->len && is_whitespace(ss->s[start])) {
				++start;
			}

			uint32_t end = start;
			while (end < ss->len && !is_whitespace(ss->s[end])) {
				++end;
			}

			if (end > start) {
				obj_array_push(wk, res, make_strn(wk, &ss->s[start], end - start));
			}

			i = end - 1;
		}
	}

	if (split) {
		s = make_strn(wk, &ss->s[start], i - start);

		obj_array_push(wk, res, s);
	}
	return res;
}

obj
str_splitlines(struct workspace *wk, const struct str *ss)
{
	const struct str seps[] = { STR("\n"), STR("\r\n"), STR("\r") };
	const struct str *split;

	obj res;
	res = make_obj(wk, obj_array);

	if (!ss->len) {
		return res;
	}

	uint32_t i, j, start = 0;
	obj s;

	for (i = 0; i < ss->len; ++i) {
		struct str slice = { .s = &ss->s[i], .len = ss->len - i };

		for (j = 0; j < ARRAY_LEN(seps); ++j) {
			split = &seps[j];

			if (str_startswith(&slice, split)) {
				s = make_strn(wk, &ss->s[start], i - start);

				obj_array_push(wk, res, s);

				start = i + split->len;
				i += split->len - 1;
				break;
			}
		}
	}

	// Only push the final element if not empty
	if (i != start) {
		s = make_strn(wk, &ss->s[start], i - start);
		obj_array_push(wk, res, s);
	}
	return res;
}

bool
str_split_in_two(const struct str *s, struct str *l, struct str *r, char split)
{
	const char *p;
	if (!(p = memchr(s->s, split, s->len))) {
		return false;
	}

	*l = (struct str){ s->s, p - s->s };
	*r = (struct str){ s->s + l->len + 1, s->len - (l->len + 1) };

	return true;
}

static bool
str_has_chr(char c, const struct str *ss)
{
	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if (ss->s[i] == c) {
			return true;
		}
	}

	return false;
}

obj
str_strip(struct workspace *wk, const struct str *ss, const struct str *strip, enum str_strip_flag flags)
{
	const struct str *defstrip = &STR(" \r\n\t");

	if (!strip) {
		strip = defstrip;
	}

	uint32_t i = 0;
	int32_t len;

	if (!(flags & str_strip_flag_right_only)) {
		for (; i < ss->len; ++i) {
			if (!str_has_chr(ss->s[i], strip)) {
				break;
			}
		}
	}

	for (len = ss->len - 1; len >= 0; --len) {
		if (len < (int64_t)i) {
			break;
		}

		if (!str_has_chr(ss->s[len], strip)) {
			break;
		}
	}
	++len;

	assert((int64_t)len >= (int64_t)i);
	return make_strn(wk, &ss->s[i], len - i);
}

struct str_split_strip_ctx {
	const struct str *strip;
	obj res;
};

static enum iteration_result
str_split_strip_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct str_split_strip_ctx *ctx = _ctx;

	obj_array_push(wk, ctx->res, str_strip(wk, get_str(wk, v), ctx->strip, 0));
	return ir_cont;
}

obj
str_split_strip(struct workspace *wk, const struct str *ss, const struct str *split, const struct str *strip)
{
	struct str_split_strip_ctx ctx = {
		.strip = strip,
	};

	ctx.res = make_obj(wk, obj_array);
	obj_array_foreach(wk, str_split(wk, ss, split), &ctx, str_split_strip_iter);
	return ctx.res;
}

/* tstr */

void
tstr_init(struct tstr *sb, char *initial_buffer, uint32_t initial_buffer_cap, enum tstr_flags flags)
{
	// If we don't get passed an initial buffer, initial_buffer_cap must be
	// zero so that the first write to this buf triggers an allocation.  As
	// a convenience, ensure the buf points to a valid empty string so that
	// callers don't have to always check len before trying to read buf.
	if (!initial_buffer) {
		assert(initial_buffer_cap == 0);
		initial_buffer = "";
	}

	if (initial_buffer_cap) {
		initial_buffer[0] = 0;
	}

	*sb = (struct tstr){
		.flags = flags,
		.buf = initial_buffer,
		.cap = initial_buffer_cap,
	};
}

void
tstr_destroy(struct tstr *sb)
{
	if ((sb->flags & tstr_flag_overflown) && (sb->flags & tstr_flag_overflow_alloc)) {
		if (sb->buf) {
			z_free(sb->buf);
			sb->buf = 0;
		}
	}
}

void
tstr_clear(struct tstr *sb)
{
	if ((sb->flags & tstr_flag_write)) {
		return;
	}

	memset(sb->buf, 0, sb->len);
	sb->len = 0;
}

void
tstr_grow(struct workspace *wk, struct tstr *sb, uint32_t inc)
{
	uint32_t newcap, newlen = sb->len + inc;

	if (newlen < sb->cap) {
		return;
	}

	newcap = sb->cap;

	if (!newcap) {
		newcap = 1024;
	}

	do {
		newcap *= 2;
	} while (newcap < newlen);

	if (sb->flags & tstr_flag_overflown) {
		if (sb->flags & tstr_flag_overflow_alloc) {
			sb->buf = z_realloc(sb->buf, newcap);
			memset((void *)&sb->buf[sb->len], 0, newcap - sb->cap);
		} else {
			grow_str(wk, &sb->s, newcap - sb->cap, false);
			struct str *ss = (struct str *)get_str(wk, sb->s);
			sb->buf = (char *)ss->s;
			ss->len = newcap;
		}
	} else {
		if (sb->flags & tstr_flag_overflow_error) {
			error_unrecoverable("unhandled tstr overflow: "
					    "capacity: %d, length: %d, "
					    "trying to push %d bytes",
				sb->cap,
				sb->len,
				inc);
		}

		sb->flags |= tstr_flag_overflown;

		char *obuf = sb->buf;

		if (sb->flags & tstr_flag_overflow_alloc) {
			sb->buf = z_calloc(newcap, 1);
		} else {
			reserve_str(wk, &sb->s, newcap);
			struct str *ss = (struct str *)get_str(wk, sb->s);
			ss->flags |= str_flag_mutable;
			sb->buf = (char *)ss->s;
			assert(ss->len == newcap);
		}

		if (obuf) {
			memcpy(sb->buf, obuf, sb->len);
		}
	}

	sb->cap = newcap;
}

void
tstr_push(struct workspace *wk, struct tstr *sb, char s)
{
	if (sb->flags & tstr_flag_write) {
		FILE *out = (FILE *)sb->buf;
		if (fputc(s, out) == EOF) {
			error_unrecoverable("failed to write output to file");
		}
		return;
	}

	tstr_grow(wk, sb, 2);

	sb->buf[sb->len] = s;
	sb->buf[sb->len + 1] = 0;
	++sb->len;
}

void
tstr_pushn(struct workspace *wk, struct tstr *sb, const char *s, uint32_t n)
{
	if (sb->flags & tstr_flag_write) {
		FILE *out = (FILE *)sb->buf;
		if (!fs_fwrite(s, n, out)) {
			error_unrecoverable("failed to write output to file");
		}
		return;
	}

	if (!n) {
		return;
	}

	tstr_grow(wk, sb, n + 1);

	memcpy(&sb->buf[sb->len], s, n);
	sb->buf[sb->len + n] = 0;
	sb->len += n;
}

void
tstr_pushs(struct workspace *wk, struct tstr *sb, const char *s)
{
	if (sb->flags & tstr_flag_write) {
		FILE *out = (FILE *)sb->buf;
		if (fputs(s, out) == EOF) {
			error_unrecoverable("failed to write output to file");
		}
		return;
	}

	uint32_t n = strlen(s) + 1;

	if (n < 2) {
		return;
	}

	tstr_grow(wk, sb, n);

	memcpy(&sb->buf[sb->len], s, n);
	sb->len += n - 1;
}

void
tstr_vpushf(struct workspace *wk, struct tstr *sb, const char *fmt, va_list args)
{
	uint32_t len;

	if (sb->flags & tstr_flag_write) {
		if (vfprintf((FILE *)sb->buf, fmt, args) < 0) {
			error_unrecoverable("failed to write output to file");
		}
		return;
	}

	va_list args_copy;
	va_copy(args_copy, args);

	len = vsnprintf(NULL, 0, fmt, args_copy);

	tstr_grow(wk, sb, len);

	vsnprintf(&sb->buf[sb->len], len + 1, fmt, args);
	sb->len += len;

	va_end(args_copy);
}

void
tstr_pushf(struct workspace *wk, struct tstr *sb, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	tstr_vpushf(wk, sb, fmt, args);

	va_end(args);
}

void
tstr_push_json_escaped(struct workspace *wk, struct tstr *buf, const char *str, uint32_t len)
{
	uint32_t i;
	for (i = 0; i < len; ++i) {
		switch (str[i]) {
		case '\b': tstr_pushs(wk, buf, "\\b"); break;
		case '\f': tstr_pushs(wk, buf, "\\f"); break;
		case '\n': tstr_pushs(wk, buf, "\\n"); break;
		case '\r': tstr_pushs(wk, buf, "\\r"); break;
		case '\t': tstr_pushs(wk, buf, "\\t"); break;
		case '"': tstr_pushs(wk, buf, "\\\""); break;
		case '\\': tstr_pushs(wk, buf, "\\\\"); break;
		default: {
			if (str[i] < ' ') {
				tstr_pushf(wk, buf, "\\u%04x", str[i]);
			} else {
				tstr_push(wk, buf, str[i]);
			}
			break;
		}
		}
	}
}

void
tstr_push_json_escaped_quoted(struct workspace *wk, struct tstr *buf, const struct str *str)
{
	tstr_push(wk, buf, '"');
	tstr_push_json_escaped(wk, buf, str->s, str->len);
	tstr_push(wk, buf, '"');
}

obj
tstr_into_str(struct workspace *wk, struct tstr *sb)
{
	assert(!(sb->flags & tstr_flag_string_exposed));

	if (!(sb->flags & tstr_flag_overflow_alloc) && sb->flags & tstr_flag_overflown) {
		sb->flags |= tstr_flag_string_exposed;
		struct str *ss = (struct str *)get_str(wk, sb->s);
		assert(strlen(sb->buf) == sb->len);
		ss->len = sb->len;
		return sb->s;
	} else if (!sb->len) {
		return make_str(wk, "");
	} else {
		return make_strn(wk, sb->buf, sb->len);
	}
}

void
tstr_trim_trailing_newline(struct tstr *sb)
{
	if (sb->buf[sb->len - 1] == '\n') {
		--sb->len;
		sb->buf[sb->len] = 0;
	}

	if (sb->len) {
		if (sb->buf[sb->len - 1] == '\r') {
			--sb->len;
			sb->buf[sb->len] = 0;
		}
	}
}

void
cstr_copy_(char *dest, const struct str *src, uint32_t dest_len)
{
	uint32_t src_len = src->len + 1;
	assert(src_len <= dest_len);
	memcpy(dest, src->s, src_len);
}

void
snprintf_append_(char *buf, uint32_t buf_len, uint32_t *buf_i, const char *fmt, ...)
{
	va_list args;

	if (*buf_i >= buf_len) {
		return;
	}

	va_start(args, fmt);
	*buf_i += vsnprintf(buf + *buf_i, buf_len - *buf_i, fmt, args);
	va_end(args);
}

/* Shlex-like string splitting
 *
 * Reference:
 * - https://docs.python.org/3/library/shlex.html
 * - https://github.com/python/cpython/blob/main/Lib/shlex.py
 * - https://pubs.opengroup.org/onlinepubs/9799919799/utilities/V3_chap02.html
 * for cmd:
 * - https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments
 * */

struct shlex_ctx {
	const struct str str;
	uint32_t i;
	char c;
};

static void
shlex_advance(struct workspace *wk, struct shlex_ctx *ctx)
{
	if (ctx->i > ctx->str.len) {
		return;
	}

	++ctx->i;
	ctx->c = ctx->str.s[ctx->i];
}

static obj
shlex_cmd_next(struct workspace *wk, struct shlex_ctx *ctx)
{
	// Arguments are delimited by whitespace characters, which are either spaces or tabs.
	while (is_whitespace(ctx->c)) {
		shlex_advance(wk, ctx);
	}

	if (!ctx->c) {
		return 0;
	}

	TSTR(tok);
	char quote = 0;
	uint32_t i, slashes;

	if (ctx->c == '"') {
		quote = '"';
		shlex_advance(wk, ctx);
	}

	while (ctx->c) {
		// A double quote mark preceded by a backslash (\") is interpreted
		// as a literal double quote mark (").
		//
		// Backslashes are interpreted literally, unless they immediately
		// precede a double quote mark.
		slashes = 0;
		while (ctx->c == '\\') {
			++slashes;
			shlex_advance(wk, ctx);
		}

		if (slashes) {
			if (ctx->c != '"') {
				for (i = 0; i < slashes; ++i) {
					tstr_push(wk, &tok, '\\');
				}
			}
		}

		if (!ctx->c) {
			goto done;
		}

		switch (ctx->c) {
		case ' ':
		case '\t': {
			if (quote) {
				tstr_push(wk, &tok, ctx->c);
			} else {
				goto done;
			}
			break;
		}
		case '"': {
			// A string surrounded by double quote marks is interpreted as a
			// single argument, whether it contains whitespace characters or
			// not. A quoted string can be embedded in an argument. The caret
			// (^) isn't recognized as an escape character or delimiter. Within
			// a quoted string, a pair of double quote marks is interpreted as
			// a single escaped double quote mark. If the command line ends
			// before a closing double quote mark is found, then all the
			// characters read so far are output as the last argument.
			if (slashes && !(slashes & 1)) {
				// If an even number of backslashes is followed by a double
				// quote mark, then one backslash (\) is placed in the argv
				// array for every pair of backslashes (\\), and the double
				// quote mark (") is interpreted as a string delimiter.
				for (i = 0; i < slashes / 2; ++i) {
					tstr_push(wk, &tok, '\\');
				}

				if (is_whitespace(ctx->str.s[ctx->i + 1])) {
					shlex_advance(wk, ctx);
					goto done;
				} else {
					quote = '"';
				}
			} else if (slashes && (slashes & 1)) {
				// If an odd number of backslashes is followed by a double
				// quote mark, then one backslash (\) is placed in the argv
				// array for every pair of backslashes (\\). The double quote
				// mark is interpreted as an escape sequence by the remaining
				// backslash, causing a literal double quote mark (") to be
				// placed in argv.
				for (i = 0; i < slashes / 2; ++i) {
					tstr_push(wk, &tok, '\\');
				}

				tstr_push(wk, &tok, '"');
			} else if (ctx->str.s[ctx->i + 1] == '"') {
				shlex_advance(wk, ctx);
				tstr_push(wk, &tok, '"');
			} else if (quote) {
				shlex_advance(wk, ctx);
				goto done;
			}
			break;
		}
		default: {
			tstr_push(wk, &tok, ctx->c);
			break;
		}
		}

		shlex_advance(wk, ctx);
	}

done:
	return tstr_into_str(wk, &tok);
}

static obj
shlex_posix_next(struct workspace *wk, struct shlex_ctx *ctx)
{
	while (is_whitespace(ctx->c)) {
		shlex_advance(wk, ctx);
	}

	if (!ctx->c) {
		return 0;
	}

	TSTR(tok);
	char quote = 0;

	while (ctx->c) {
		switch (ctx->c) {
		case '#': {
			// If the current character is a '#', it and all subsequent
			// characters up to, but excluding, the next <newline> shall be
			// discarded as a comment. The <newline> that ends the line is not
			// considered part of the comment.
			if (quote) {
				tstr_push(wk, &tok, ctx->c);
			} else {
				while (ctx->c && ctx->c != '\n') {
					shlex_advance(wk, ctx);
				}
				continue;
			}
			break;
		}
		case ' ':
		case '\t':
		case '\n': {
			if (quote) {
				tstr_push(wk, &tok, ctx->c);
			} else {
				goto done;
			}
			break;
		}
		case '\\': {
			shlex_advance(wk, ctx);
			// 2.2.1 Escape Character (Backslash)
			// A <backslash> that is not quoted shall preserve the literal
			// value of the following character, with the exception of a
			// <newline>.
			if (quote == '\'') {
				tstr_push(wk, &tok, '\\');
				tstr_push(wk, &tok, ctx->c);
			} else if (quote == '"') {
				// Outside of "$(...)" and "${...}" the <backslash> shall
				// retain its special meaning as an escape character (see 2.2.1
				// Escape Character (Backslash)) only when immediately followed
				// by one of the following characters:

				// $   `   \   <newline>

				// or by a double-quote character that would otherwise be
				// considered special (see 2.6.4 Arithmetic Expansion and 2.7.4
				// Here-Document).

				if (strchr("$`\\\n\"", ctx->c)) {
					tstr_push(wk, &tok, ctx->c);
				} else {
					tstr_push(wk, &tok, '\\');
					tstr_push(wk, &tok, ctx->c);
				}
			} else if (ctx->c == '\n') {
				// If a <newline> immediately follows the <backslash>,
				// the shell shall interpret this as line continuation. The
				// <backslash> and <newline> shall be removed before splitting the
				// input into tokens.
			} else {
				tstr_push(wk, &tok, ctx->c);
			}
			break;
		}
		case '\'': {
			// 2.2.2 Single-Quotes
			// Enclosing characters in single-quotes ('') shall preserve the
			// literal value of each character within the single-quotes. A
			// single-quote cannot occur within single-quotes.
			if (!quote) {
				quote = '\'';
			} else if (quote == '\'') {
				quote = 0;
			} else if (quote == '\"') {
				tstr_push(wk, &tok, ctx->c);
			}
			break;
		}
		case '"': {
			// 2.2.3 Double-Quotes
			// Enclosing characters in double-quotes ("") shall preserve the
			// literal value of all characters within the double-quotes, with
			// the exception of the characters backquote, <dollar-sign>, and
			// <backslash>, as follows:
			if (!quote) {
				quote = '\"';
			} else if (quote == '\"') {
				quote = 0;
			} else if (quote == '\'') {
				tstr_push(wk, &tok, ctx->c);
			}
			break;
		}
		default: {
			tstr_push(wk, &tok, ctx->c);
			break;
		}
		}

		shlex_advance(wk, ctx);
	}

done:
	return tstr_into_str(wk, &tok);
}

enum shell_type
shell_type_for_host_machine(void)
{
	if (host_machine.is_windows) {
		return shell_type_cmd;
	} else {
		return shell_type_posix;
	}
}

obj
str_shell_split(struct workspace *wk, const struct str *str, enum shell_type shell)
{
	obj tok, res = make_obj(wk, obj_array);

	struct shlex_ctx ctx = { .str = *str, .c = str->s[0] };

	obj (*lex_func)(struct workspace *wk, struct shlex_ctx *ctx) = 0;

	switch (shell) {
	case shell_type_posix: lex_func = shlex_posix_next; break;
	case shell_type_cmd: lex_func = shlex_cmd_next; break;
	}

	while ((tok = lex_func(wk, &ctx))) {
		obj_array_push(wk, res, tok);
	}

	return res;
}

static int32_t
min3(int32_t a, int32_t b, int32_t c)
{
	int32_t min = a;
	if (b < min) {
		min = b;
	}
	if (c < min) {
		min = c;
	}
	return min;
}

#define LEVENSHTEIN_MAX_COMPARE_LEN 256

static int32_t
str_levenshtein_distance(const struct str *a, const struct str *b)
{
	int32_t *v0, *v1, _v0[LEVENSHTEIN_MAX_COMPARE_LEN] = { 0 }, _v1[LEVENSHTEIN_MAX_COMPARE_LEN] = { 0 };
	v0 = _v0;
	v1 = _v1;

	int32_t m = a->len + 1, n = b->len + 1;
	if (m > LEVENSHTEIN_MAX_COMPARE_LEN) {
		m = LEVENSHTEIN_MAX_COMPARE_LEN;
	}
	if (n > LEVENSHTEIN_MAX_COMPARE_LEN) {
		n = LEVENSHTEIN_MAX_COMPARE_LEN;
	}

	for (int32_t i = 0; i < n; ++i) {
		v0[i] = i;
	}

	for (int32_t i = 0; i < m - 1; ++i) {
		v1[0] = i + 1;

		for (int32_t j = 0; j < n - 1; ++j) {
			int32_t deletionCost = v0[j + 1] + 1;
			int32_t insertionCost = v1[j] + 1;
			int32_t substitutionCost;
			if (str_char_to_lower(a->s[i]) == str_char_to_lower(b->s[j])) {
				substitutionCost = v0[j];
			} else {
				substitutionCost = v0[j] + 1;
			}

			v1[j + 1] = min3(deletionCost, insertionCost, substitutionCost);
		}

		if (v0 == _v0) {
			v0 = _v1;
			v1 = _v0;
		} else {
			v0 = _v0;
			v1 = _v1;
		}
	}

	return v0[n - 1];
}

#define JARO_WINKLER_MAX_COMPARE_LEN 64

static double
str_jaro_winkler_distance(const struct str *s1, const struct str *s2)
{
	if (s1->len > s2->len) {
		const struct str *s3 = s1;
		s1 = s2;
		s2 = s3;
	}
	int32_t length1 = s1->len, length2 = s2->len;

	if (length1 > JARO_WINKLER_MAX_COMPARE_LEN) {
		length1 = JARO_WINKLER_MAX_COMPARE_LEN;
	}
	if (length2 > JARO_WINKLER_MAX_COMPARE_LEN) {
		length2 = JARO_WINKLER_MAX_COMPARE_LEN;
	}

	double m = 0, t = 0;
	int32_t range = length1 > 3 ? length2 / 2 - 1 : 0;
	uint64_t flags1 = 0, flags2 = 0;

	for (int32_t i = 0; i < length1; ++i) {
		int32_t last = i + range;
		for (int32_t j = i >= range ? i - range : 0; j < last; ++j) {
			if (!(flags2 & (1 << j)) && str_char_to_lower(s1->s[i]) == str_char_to_lower(s2->s[j])) {
				flags1 |= 1 << i;
				flags2 |= 1 << j;
				m += 1;
				break;
			}
		}
	}

	if (m == 0.0) {
		return m;
	}

	int32_t k = 0;
	for (int32_t i = 0; i < length1; ++i) {
		if ((flags1 & (1 << i))) {
			int32_t index = k;
			for (int32_t j = k; j < length2; ++j) {
				index = j;

				if ((flags2 & (1 << j))) {
					k = j + 1;
					break;
				}
			}

			if (str_char_to_lower(s1->s[i]) != str_char_to_lower(s2->s[index])) {
				t += 0.5;
			}
		}
	}

	double dist = m == 0 ? 0 : (m / length1 + m / length2 + (m - t) / m) / 3;

	if (dist > 0.7) {
		double prefix_bonus = 0.0;

		for (int32_t i = 0; i < length1 && i < 4; ++i) {
			if (str_char_to_lower(s1->s[i]) == str_char_to_lower(s2->s[i])) {
				prefix_bonus += 0.25;
			} else {
				break;
			}
		}

		dist += prefix_bonus * (1 - dist);
	}

	return dist;
}

bool
str_fuzzy_match(const struct str *input, const struct str *guess, int32_t *dist)
{
	{
		double threshold = input->len > 3 ? 0.834 : 0.77;
		if (str_jaro_winkler_distance(input, guess) < threshold) {
			return false;
		}
	}

	{
		int32_t threshold = (input->len * 0.25) + 0.5;
		if ((*dist = str_levenshtein_distance(input, guess)) > threshold) {
			return false;
		}
	}

	return true;
}
