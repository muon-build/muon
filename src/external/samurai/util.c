/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "error.h"
#include "external/samurai/ctx.h"
#include "lang/string.h"
#include "log.h"
#include "platform/mem.h"
#include "platform/path.h"

#include "external/samurai/util.h"

static void
samu_vwarn(const char *fmt, va_list ap)
{
	fprintf(stderr, "samu: ");
	vfprintf(stderr, fmt, ap);
	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		putc(' ', stderr);
		perror(NULL);
	} else {
		putc('\n', stderr);
	}
}

void
samu_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	samu_vwarn(fmt, ap);
	va_end(ap);
}

void
samu_fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	samu_vwarn(fmt, ap);
	va_end(ap);
	exit(1);
}

int
samu_vprintf(struct samu_ctx *ctx, const char *fmt, va_list ap)
{
	return vfprintf(ctx->out, fmt, ap);
}

int
samu_printf(struct samu_ctx *ctx, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = samu_vprintf(ctx, fmt, ap);
	va_end(ap);
	return r;
}

void
samu_puts_no_newline(struct samu_ctx *ctx, const char *str)
{
	fputs(str, ctx->out);
}

void
samu_puts(struct samu_ctx *ctx, const char *str)
{
	fputs(str, ctx->out);
	fputc('\n', ctx->out);
}

void
samu_putchar(struct samu_ctx *ctx, const char c)
{
	fputc(c, ctx->out);
}

void *
samu_xmalloc(struct samu_arena *a, size_t n)
{
	return samu_arena_alloc(a, n);
}

static void
samu_arena_push_block(struct samu_arena *a, uint64_t size)
{
	++a->blocks_len;
	a->blocks = z_realloc(a->blocks, sizeof(const char *) * a->blocks_len);
	a->allocd += size;
	a->blocks[a->blocks_len - 1] = z_calloc(1, size);
}

#define SAMU_ARENA_DEF_BLOCK_SIZE BUF_SIZE_1m

void
samu_arena_init(struct samu_arena *a)
{
	samu_arena_push_block(a, SAMU_ARENA_DEF_BLOCK_SIZE);
}

void
samu_arena_destroy(struct samu_arena *a)
{
	size_t i;
	for (i = 0; i < a->blocks_len; ++i) {
		z_free(a->blocks[i]);
	}

	L("samu allocd %d blocks, a:%d, f:%d, r:%3.3f\n", (int)a->blocks_len, (int)a->allocd, (int)a->filled, (float)a->filled / (float)a->allocd * 100.0f);

	z_free(a->blocks);
}

void *
samu_arena_alloc(struct samu_arena *a, size_t size)
{
	uint64_t align = -a->i & 7;
	a->i += align;

	if (a->i + size > SAMU_ARENA_DEF_BLOCK_SIZE || size > SAMU_ARENA_DEF_BLOCK_SIZE) {
		uint64_t new_block_size = size > SAMU_ARENA_DEF_BLOCK_SIZE ? size : SAMU_ARENA_DEF_BLOCK_SIZE;

		samu_arena_push_block(a, new_block_size);
		a->i = 0;
	}

	a->filled += size;

	char *mem = a->blocks[a->blocks_len - 1] + a->i;
	a->i += align + size;
	return mem;
}


void *
samu_arena_realloc(struct samu_arena *a, void *p, size_t old, size_t new)
{
	char *mem = samu_arena_alloc(a, new);
	if (p) {
		memcpy(mem, p, old);
	}
	return mem;
}

static void *
samu_reallocarray(struct samu_arena *a, void *p, size_t old, size_t new, size_t m)
{
	if (m && new > SIZE_MAX / m) {
		errno = ENOMEM;
		return NULL;
	}
	return samu_arena_realloc(a, p, old * m, new * m);
}

void *
samu_xreallocarray(struct samu_arena *a, void *p, size_t old, size_t new, size_t m)
{
	return samu_reallocarray(a, p, old, new, m);
}

char *
samu_xmemdup(struct samu_arena *a, const char *s, size_t n)
{
	char *p;

	p = samu_xmalloc(a, n);
	memcpy(p, s, n);

	return p;
}

int
samu_xasprintf(struct samu_arena *a, char **s, const char *fmt, ...)
{
	va_list ap;
	int ret;
	size_t n;

	va_start(ap, fmt);
	ret = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	assert(!(ret < 0));
	n = ret + 1;
	*s = samu_xmalloc(a, n);
	va_start(ap, fmt);
	ret = vsnprintf(*s, n, fmt, ap);
	va_end(ap);
	assert(!(ret < 0 || (size_t)ret >= n));

	return ret;
}

void
samu_bufadd(struct samu_arena *a, struct samu_buffer *buf, char c)
{
	if (buf->len >= buf->cap) {
		size_t newcap = buf->cap ? buf->cap * 2 : 1 << 8;
		buf->data = samu_arena_realloc(a, buf->data, buf->cap, newcap);
		buf->cap = newcap;
	}
	buf->data[buf->len++] = c;
}

struct samu_string *
samu_mkstr(struct samu_arena *a, size_t n)
{
	struct samu_string *str;

	str = samu_xmalloc(a, sizeof(*str) + n + 1);
	str->n = n;

	return str;
}

void
samu_canonpath(struct samu_string *path)
{
	char *component[60];
	int n;
	char *s, *d, *end;

	if (path->n == 0) {
		samu_fatal("empty path");
	}
	s = d = path->s;
	end = path->s + path->n;
	n = 0;
	if (*s == '/') {
		++s;
		++d;
	}
	while (s < end) {
		switch (s[0]) {
		case '/':
			++s;
			continue;
		case '.':
			switch (s[1]) {
			case '\0': case '/':
				s += 2;
				continue;
			case '.':
				if (s[2] != '/' && s[2] != '\0') {
					break;
				}
				if (n > 0) {
					d = component[--n];
				} else {
					*d++ = s[0];
					*d++ = s[1];
					*d++ = s[2];
				}
				s += 3;
				continue;
			}
		}
		if (n == ARRAY_LEN(component)) {
			samu_fatal("path has too many components: %s", path->s);
		}
		component[n++] = d;
		while (*s != '/' && *s != '\0') {
			*d++ = *s++;
		}
		*d++ = *s++;
	}
	if (d == path->s) {
		*d++ = '.';
		*d = '\0';
	} else {
		*--d = '\0';
	}
	path->n = d - path->s;
}

int
samu_makedirs(struct samu_string *path, bool parent)
{
	bool ok = true;
	if (parent) {
		SBUF_manual(dirname);
		path_dirname(0, &dirname, path->s);
		ok = fs_mkdir_p(dirname.buf);
		sbuf_destroy(&dirname);
	} else if (!fs_dir_exists(path->s)) {
		ok = fs_mkdir(path->s);
	}

	return ok ? 0 : -1;
}

int
samu_writefile(const char *name, struct samu_string *s)
{
	if (!fs_write(name, (uint8_t *)s->s, s->n)) {
		return -1;
	}

	return 0;
}
