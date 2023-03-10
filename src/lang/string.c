/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "lang/object.h"
#include "lang/string.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/mem.h"

void
str_unescape(struct workspace *wk, struct sbuf *sb, const struct str *ss,
	bool escape_whitespace)
{
	bool esc;
	uint32_t i;

	for (i = 0; i < ss->len; ++i) {
		esc = ss->s[i] < 32;
		if (!escape_whitespace && strchr("\t\n\r", ss->s[i])) {
			esc = false;
		}

		if (esc) {
			sbuf_pushf(wk, sb, "\\%d", ss->s[i]);
		} else {
			sbuf_push(wk, sb, ss->s[i]);
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
grow_str(struct workspace *wk, obj s, uint32_t grow_by, bool alloc_nul)
{
	assert(s);

	struct str *ss = (struct str *)get_str(wk, s);
	uint32_t new_len = ss->len + grow_by;

	if (alloc_nul) {
		new_len += 1;
	}

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
	/* obj_vsnprintf(wk, (char *)ss->s, len + 1, fmt, args); */
	vsnprintf((char *)ss->s, len + 1, fmt, args);

	va_end(args_copy);
	va_end(args);

	return s;
}

void
str_appn(struct workspace *wk, obj s, const char *str, uint32_t n)
{
	struct str *ss = grow_str(wk, s, n, true);
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

	uint32_t olen = get_str(wk, s)->len;
	struct str *ss = grow_str(wk, s, len, true);

	/* obj_vsnprintf(wk, (char *)ss->s, len + 1, fmt, args); */
	vsnprintf((char *)&ss->s[olen], len + 1, fmt, args);
	ss->len += len;

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
	if ((uint32_t)(endptr - ss->s) != ss->len) {
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
	const struct str *defstrip = &WKSTR(" \n\t");

	if (!strip) {
		strip = defstrip;
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

/* sbuf */

void
sbuf_init(struct sbuf *sb, char *initial_buffer, uint32_t initial_buffer_cap,
	enum sbuf_flags flags)
{
	if (initial_buffer_cap) {
		initial_buffer[0] = 0;
	}

	*sb = (struct sbuf) {
		.flags = flags,
		.buf = initial_buffer,
		.cap = initial_buffer_cap,
	};
}

void
sbuf_destroy(struct sbuf *sb)
{
	if ((sb->flags & sbuf_flag_overflown)
	    && (sb->flags & sbuf_flag_overflow_alloc)) {
		z_free(sb->buf);
	}
}

void
sbuf_clear(struct sbuf *sb)
{
	if ((sb->flags & sbuf_flag_write)) {
		return;
	}

	memset(sb->buf, 0, sb->len);
	sb->len = 0;
}

void
sbuf_grow(struct workspace *wk, struct sbuf *sb, uint32_t inc)
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

	if (sb->flags & sbuf_flag_overflown) {
		if (sb->flags & sbuf_flag_overflow_alloc) {
			sb->buf = z_realloc(sb->buf, newcap);
			memset((void *)&sb->buf[sb->len], 0, newcap - sb->cap);
		} else {
			grow_str(wk, sb->s, newcap - sb->cap, false);
			struct str *ss = (struct str *)get_str(wk, sb->s);
			sb->buf = (char *)ss->s;
			ss->len = newcap;
		}
	} else {
		if (sb->flags & sbuf_flag_overflow_error) {
			error_unrecoverable(
				"unhandled sbuf overflow: "
				"capacity: %d, length: %d, "
				"trying to push %d bytes",
				sb->cap, sb->len, inc);
		}

		sb->flags |= sbuf_flag_overflown;

		char *obuf = sb->buf;

		if (sb->flags & sbuf_flag_overflow_alloc) {
			sb->buf = z_calloc(newcap, 1);
		} else {
			reserve_str(wk, &sb->s, newcap);
			const struct str *ss = get_str(wk, sb->s);
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
sbuf_push(struct workspace *wk, struct sbuf *sb, char s)
{
	if (sb->flags & sbuf_flag_write) {
		if (fputc(s, (FILE *)sb->buf) == EOF) {
			error_unrecoverable("failed to write output to file");
		}
		return;
	}

	sbuf_grow(wk, sb, 2);

	sb->buf[sb->len] = s;
	sb->buf[sb->len + 1] = 0;
	++sb->len;
}

void
sbuf_pushn(struct workspace *wk, struct sbuf *sb, const char *s, uint32_t n)
{
	if (sb->flags & sbuf_flag_write) {
		if (!fs_fwrite(s, n, (FILE *)sb->buf)) {
			error_unrecoverable("failed to write output to file");
		}
		return;
	}

	if (!n) {
		return;
	}

	sbuf_grow(wk, sb, n + 1);

	memcpy(&sb->buf[sb->len], s, n);
	sb->buf[sb->len + n] = 0;
	sb->len += n;
}

void
sbuf_pushs(struct workspace *wk, struct sbuf *sb, const char *s)
{
	if (sb->flags & sbuf_flag_write) {
		if (fputs(s, (FILE *)sb->buf) == EOF) {
			error_unrecoverable("failed to write output to file");
		}
		return;
	}

	uint32_t n = strlen(s) + 1;

	if (n < 2) {
		return;
	}

	sbuf_grow(wk, sb, n);

	memcpy(&sb->buf[sb->len], s, n);
	sb->len += n - 1;
}

void
sbuf_pushf(struct workspace *wk, struct sbuf *sb, const char *fmt, ...)
{
	uint32_t len;
	va_list args, args_copy;
	va_start(args, fmt);

	if (sb->flags & sbuf_flag_write) {
		if (vfprintf((FILE *)sb->buf, fmt, args) < 0) {
			error_unrecoverable("failed to write output to file");
		}
		goto done;
	}

	va_copy(args_copy, args);

	len = vsnprintf(NULL, 0, fmt, args_copy);

	sbuf_grow(wk, sb, len);

	vsnprintf(&sb->buf[sb->len], len + 1, fmt, args);
	sb->len += len;

	va_end(args_copy);

done:
	va_end(args);
}

obj
sbuf_into_str(struct workspace *wk, struct sbuf *sb)
{
	assert(!(sb->flags & sbuf_flag_string_exposed)
		&& !(sb->flags & sbuf_flag_overflow_alloc));

	if (sb->flags & sbuf_flag_overflown) {
		sb->flags |= sbuf_flag_string_exposed;
		struct str *ss = (struct str *)get_str(wk, sb->s);
		assert(strlen(sb->buf) == sb->len);
		ss->len = sb->len;
		return sb->s;
	} else {
		return make_strn(wk, sb->buf, sb->len);
	}
}
