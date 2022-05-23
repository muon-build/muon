#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "lang/object.h"
#include "lang/string.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/mem.h"

bool
buf_push(char *buf, uint32_t len, uint32_t *i, char s)
{
	if (*i >= len) {
		return false;
	}

	buf[*i] = s;
	++(*i);
	return true;
}

bool
buf_pushs(char *buf, uint32_t len, uint32_t *i, char *s)
{
	for (; *s; ++s) {
		if (!buf_push(buf, len, i, *s)) {
			return false;
		}
	}

	return true;
}

bool
buf_pushn(char *buf, uint32_t len, uint32_t *i, char *s, uint32_t n)
{
	uint32_t j;
	for (j = 0; j < n; ++j) {
		if (!buf_push(buf, len, i, s[j])) {
			return false;
		}
	}

	return true;
}

bool
str_unescape(char *buf, uint32_t len, const struct str *ss, uint32_t *r)
{
	uint32_t i;
	*r = 0;

	for (i = 0; i < ss->len; ++i) {
		if (ss->s[i] < 32) {
			char unescaped[32];
			uint32_t n = snprintf(unescaped, 32, "\\%d", ss->s[i]);

			if (!buf_pushn(buf, len, r, unescaped, n)) {
				return false;
			}
		} else {
			if (!buf_push(buf, len, r, ss->s[i])) {
				return false;
			}
		}
	}
	return true;
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

	assert(!str_has_null(ss) && "cstr can not contain null bytes");

	return ss->s;
}

static struct str *
grow_str(struct workspace *wk, obj s, uint32_t grow_by)
{
	assert(s);

	struct str *ss = (struct str *)get_str(wk, s);
	uint32_t new_len = ss->len + grow_by + 1;

	if (ss->flags & str_flag_big) {
		ss->s = z_realloc((void *)ss->s, new_len);
		memset((void *)&ss->s[ss->len], 0, new_len - ss->len);
	} else if (new_len >= wk->chrs.bucket_size) {
		ss->flags |= str_flag_big;
		char *np = z_calloc(new_len, 1);
		memcpy(np, ss->s, ss->len);
		ss->s = np;
	} else {
		char *np = bucket_array_pushn(&wk->chrs, ss->s, ss->len, new_len);
		ss->s = np;
	}

	return ss;
}

static struct str *
reserve_str(struct workspace *wk, obj *s, uint32_t len)
{
	enum str_flags f = 0;
	const char *p;

	uint32_t new_len = len + 1;

	if (new_len > wk->chrs.bucket_size) {
		f |= str_flag_big;
		p = z_calloc(new_len, 1);
	} else {
		p = bucket_array_pushn(&wk->chrs, NULL, 0, new_len);
	}

	make_obj(wk, s, obj_string);

	struct str *str = (struct str*)get_str(wk, *s);

	*str = (struct str) {
		.s = p,
		.len = len,
		.flags = f,
	};

	return str;
}

static obj
_make_str(struct workspace *wk, const char *p, uint32_t len)
{
	obj s;

	if (!p) {
		return 0;
	}

	memcpy((void *)reserve_str(wk, &s, len)->s, p, len);
	return s;
}

obj
make_strn(struct workspace *wk, const char *str, uint32_t n)
{
	return _make_str(wk, str, n);
}

obj
make_str(struct workspace *wk, const char *str)
{
	return _make_str(wk, str, strlen(str));
}

obj
make_strf(struct workspace *wk, const char *fmt, ...)
{
	uint32_t len;
	va_list args, args_copy;
	va_start(args, fmt);
	va_copy(args_copy, args);

	len = vsnprintf(NULL, 0, fmt, args_copy);

	obj s;
	struct str *ss = reserve_str(wk, &s, len);
	// TODO: the buffer size is too small here because the object expansion
	// isn't taken in to account by vsnprintf above.  Need to make it
	// possible to pass NULL to obj_vsnprintf to get a reliable buffer
	// length
	obj_vsnprintf(wk, (char *)ss->s, len + 1, fmt, args);

	va_end(args_copy);
	va_end(args);

	return s;
}

void
str_appn(struct workspace *wk, obj s, const char *str, uint32_t n)
{
	struct str *ss = grow_str(wk, s, n);
	memcpy((char *)&ss->s[ss->len], str, n);
	ss->len += n;
}

void
str_app(struct workspace *wk, obj s, const char *str)
{
	str_appn(wk, s, str, strlen(str));
}

void
str_appf(struct workspace *wk, obj s, const char *fmt, ...)
{
	uint32_t len;
	va_list args, args_copy;
	va_start(args, fmt);
	va_copy(args_copy, args);

	len = vsnprintf(NULL, 0, fmt, args_copy);

	struct str *ss = grow_str(wk, s, len);

	obj_vsnprintf(wk, (char *)ss->s, len + 1, fmt, args);

	va_end(args_copy);
	va_end(args);
}

obj
str_clone(struct workspace *wk_src, struct workspace *wk_dest, obj val)
{
	const struct str *ss = get_str(wk_src, val);
	return make_strn(wk_dest, ss->s, ss->len);
}

bool
str_eql(const struct str *ss1, const struct str *ss2)
{
	return ss1->len == ss2->len && memcmp(ss1->s, ss2->s, ss1->len) == 0;
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
str_endswith(const struct str *ss, const struct str *suf)
{
	if (ss->len < suf->len) {
		return false;
	}

	return memcmp(&ss->s[ss->len - suf->len], suf->s, suf->len) == 0;
}

obj
str_join(struct workspace *wk, obj s1, obj s2)
{
	obj res;
	const struct str *ss1 = get_str(wk, s1),
			 *ss2 = get_str(wk, s2);

	struct str *ss = reserve_str(wk, &res, ss1->len + ss2->len);

	memcpy((char *)ss->s, ss1->s, ss1->len);
	memcpy((char *)&ss->s[ss1->len], ss2->s, ss2->len);

	return res;
}

bool
str_to_i(const struct str *ss, int64_t *res)
{
	char *endptr = NULL;
	*res = strtol(ss->s, &endptr, 10);
	if (endptr - ss->s != ss->len) {
		return false;
	}

	return true;
}

obj
str_split(struct workspace *wk, const struct str *ss, const struct str *split)
{
	static const char *whitespace = "\n\r\t ";
	obj res;
	make_obj(wk, &res, obj_array);

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
			while (start < ss->len && strchr(whitespace, ss->s[start])) {
				++start;
			}

			uint32_t end = start;
			while (end < ss->len && !strchr(whitespace, ss->s[end])) {
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
str_strip(struct workspace *wk, const struct str *ss, const struct str *strip)
{
	if (!strip) {
		strip = &WKSTR(" \n\t");
	}

	uint32_t i;
	int32_t len;

	for (i = 0; i < ss->len; ++i) {
		if (!str_has_chr(ss->s[i], strip)) {
			break;
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

	obj_array_push(wk, ctx->res, str_strip(wk, get_str(wk, v), ctx->strip));
	return ir_cont;
}

obj
str_split_strip(struct workspace *wk,
	const struct str *ss,
	const struct str *split,
	const struct str *strip)
{
	struct str_split_strip_ctx ctx = {
		.strip = strip,
	};

	make_obj(wk, &ctx.res, obj_array);
	obj_array_foreach(wk, str_split(wk, ss, split), &ctx, str_split_strip_iter);
	return ctx.res;
}
