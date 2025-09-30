/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "external/samurai/ctx.h"

#include "external/samurai/scan.h"
#include "external/samurai/util.h"

void
samu_scaninit(struct samu_ctx *ctx, struct samu_scanner *s, const char *path)
{
	*s = (struct samu_scanner) {
		.path = path,
		.line = 1,
		.col = 1,
		.src_i = 1,
	};

	if (!fs_read_entire_file(ctx->a, path, &s->src)) {
		samu_fatal("failed to read %s", path);
	}

	s->chr = s->src.src[0];
}

void
samu_scanerror(struct samu_scanner *s, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "samu: %s:%d:%d: ", s->path, s->line, s->col);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	putc('\n', stderr);
	exit(1);
}

static int
samu_next(struct samu_scanner *s)
{
	if (s->chr == '\n') {
		++s->line;
		s->col = 1;
	} else {
		++s->col;
	}
	if (s->src_i < s->src.len) {
		s->chr = s->src.src[s->src_i];
		++s->src_i;
	} else {
		s->chr = EOF;
	}

	return s->chr;
}

static int
samu_issimplevar(int c)
{
	return isalnum(c) || c == '_' || c == '-';
}

static int
samu_isvar(int c)
{
	return samu_issimplevar(c) || c == '.';
}

static bool
samu_newline(struct samu_scanner *s)
{
	switch (s->chr) {
	case '\r':
		if (samu_next(s) != '\n')
			samu_scanerror(s, "expected '\\n' after '\\r'");
		/* fallthrough */
	case '\n':
		samu_next(s);
		return true;
	}
	return false;
}

static bool
samu_singlespace(struct samu_scanner *s)
{
	switch (s->chr) {
	case '$':
		samu_next(s);
		if (samu_newline(s))
			return true;
		--s->src_i;
		s->chr = '$';
		return false;
	case ' ':
		samu_next(s);
		return true;
	}
	return false;
}

static bool
samu_space(struct samu_scanner *s)
{
	if (!samu_singlespace(s))
		return false;
	while (samu_singlespace(s))
		;
	return true;
}

static bool
samu_comment(struct samu_scanner *s)
{
	if (s->chr != '#')
		return false;
	do samu_next(s);
	while (!samu_newline(s));
	return true;
}

static void
samu_name(struct samu_ctx *ctx, struct samu_scanner *s)
{
	ctx->scan.buf.len = 0;
	while (samu_isvar(s->chr)) {
		samu_bufadd(&ctx->arena, &ctx->scan.buf, s->chr);
		samu_next(s);
	}
	if (!ctx->scan.buf.len)
		samu_scanerror(s, "expected name");
	samu_bufadd(&ctx->arena, &ctx->scan.buf, '\0');
	samu_space(s);
}

int
samu_scankeyword(struct samu_ctx *ctx, struct samu_scanner *s, char **var)
{
	/* must stay in sorted order */
	static const struct {
		const char *name;
		int value;
	} keywords[] = {
		{"build",    SAMU_BUILD},
		{"default",  SAMU_DEFAULT},
		{"include",  SAMU_INCLUDE},
		{"pool",     SAMU_POOL},
		{"rule",     SAMU_RULE},
		{"subninja", SAMU_SUBNINJA},
	};
	int low = 0, high = ARRAY_LEN(keywords) - 1, mid, cmp;

	for (;;) {
		switch (s->chr) {
		case ' ':
			samu_space(s);
			if (!samu_comment(s) && !samu_newline(s))
				samu_scanerror(s, "unexpected indent");
			break;
		case '#':
			samu_comment(s);
			break;
		case '\r':
		case '\n':
			samu_newline(s);
			break;
		case EOF:
			return EOF;
		default:
			samu_name(ctx, s);
			while (low <= high) {
				mid = (low + high) / 2;
				cmp = strcmp(ctx->scan.buf.data, keywords[mid].name);
				if (cmp == 0)
					return keywords[mid].value;
				if (cmp < 0)
					high = mid - 1;
				else
					low = mid + 1;
			}
			*var = samu_xmemdup(&ctx->arena, ctx->scan.buf.data, ctx->scan.buf.len);
			return SAMU_VARIABLE;
		}
	}
}

char *
samu_scanname(struct samu_ctx *ctx, struct samu_scanner *s)
{
	samu_name(ctx, s);
	return samu_xmemdup(&ctx->arena, ctx->scan.buf.data, ctx->scan.buf.len);
}

static void
samu_addstringpart(struct samu_ctx *ctx, struct samu_evalstring ***end, bool var)
{
	struct samu_evalstring *p;

	p = samu_xmalloc(&ctx->arena, sizeof(*p));
	p->next = NULL;
	**end = p;
	if (var) {
		samu_bufadd(&ctx->arena, &ctx->scan.buf, '\0');
		p->var = samu_xmemdup(&ctx->arena, ctx->scan.buf.data, ctx->scan.buf.len);
	} else {
		p->var = NULL;
		p->str = samu_mkstr(&ctx->arena, ctx->scan.buf.len);
		memcpy(p->str->s, ctx->scan.buf.data, ctx->scan.buf.len);
		p->str->s[ctx->scan.buf.len] = '\0';
	}
	*end = &p->next;
	ctx->scan.buf.len = 0;
}

static void
samu_escape(struct samu_ctx *ctx, struct samu_scanner *s, struct samu_evalstring ***end)
{
	switch (s->chr) {
	case '$':
	case ' ':
	case ':':
		samu_bufadd(&ctx->arena, &ctx->scan.buf, s->chr);
		samu_next(s);
		break;
	case '{':
		if (ctx->scan.buf.len > 0)
			samu_addstringpart(ctx, end, false);
		while (samu_isvar(samu_next(s)))
			samu_bufadd(&ctx->arena, &ctx->scan.buf, s->chr);
		if (s->chr != '}')
			samu_scanerror(s, "invalid variable name");
		samu_next(s);
		samu_addstringpart(ctx, end, true);
		break;
	case '\r':
	case '\n':
		samu_newline(s);
		samu_space(s);
		break;
	default:
		if (ctx->scan.buf.len > 0)
			samu_addstringpart(ctx, end, false);
		while (samu_issimplevar(s->chr)) {
			samu_bufadd(&ctx->arena, &ctx->scan.buf, s->chr);
			samu_next(s);
		}
		if (!ctx->scan.buf.len)
			samu_scanerror(s, "invalid $ escape");
		samu_addstringpart(ctx, end, true);
	}
}

struct samu_evalstring *
samu_scanstring(struct samu_ctx *ctx, struct samu_scanner *s, bool path)
{
	struct samu_evalstring *str = NULL, **end = &str;

	ctx->scan.buf.len = 0;
	for (;;) {
		switch (s->chr) {
		case '$':
			samu_next(s);
			samu_escape(ctx, s, &end);
			break;
		case ':':
		case '|':
		case ' ':
			if (path)
				goto out;
			/* fallthrough */
		default:
			samu_bufadd(&ctx->arena, &ctx->scan.buf, s->chr);
			samu_next(s);
			break;
		case '\r':
		case '\n':
		case EOF:
			goto out;
		}
	}
out:
	if (ctx->scan.buf.len > 0)
		samu_addstringpart(ctx, &end, 0);
	if (path)
		samu_space(s);
	return str;
}

void
samu_scanpaths(struct samu_ctx *ctx, struct samu_scanner *s)
{
	struct samu_evalstring *str;

	while ((str = samu_scanstring(ctx, s, true))) {
		if (ctx->scan.npaths == ctx->scan.paths_max) {
			size_t newmax = ctx->scan.paths_max ? ctx->scan.paths_max * 2 : 32;
			ctx->scan.paths = samu_xreallocarray(&ctx->arena, ctx->scan.paths, ctx->scan.paths_max, newmax, sizeof(ctx->scan.paths[0]));
			ctx->scan.paths_max = newmax;
		}
		ctx->scan.paths[ctx->scan.npaths++] = str;
	}
}

void
samu_scanchar(struct samu_scanner *s, int c)
{
	if (s->chr != c)
		samu_scanerror(s, "expected '%c'", c);
	samu_next(s);
	samu_space(s);
}

int
samu_scanpipe(struct samu_scanner *s, int n)
{
	if (s->chr != '|')
		return 0;
	samu_next(s);
	if (s->chr != '|') {
		if (!(n & 1))
			samu_scanerror(s, "expected '||'");
		samu_space(s);
		return 1;
	}
	if (!(n & 2))
		samu_scanerror(s, "unexpected '||'");
	samu_next(s);
	samu_space(s);
	return 2;
}

bool
samu_scanindent(struct samu_scanner *s)
{
	bool indent;

	for (;;) {
		indent = samu_space(s);
		if (!samu_comment(s))
			return indent && !samu_newline(s);
	}
}

void
samu_scannewline(struct samu_scanner *s)
{
	if (!samu_newline(s))
		samu_scanerror(s, "expected newline");
}
