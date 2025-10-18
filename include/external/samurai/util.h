/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_UTIL_H
#define MUON_EXTERNAL_SAMU_UTIL_H
void samu_warn(const char *, ...)
MUON_ATTR_FORMAT(printf, 1, 2);
void samu_fatal(const char *, ...)
MUON_ATTR_FORMAT(printf, 1, 2);

int samu_vprintf(struct samu_ctx *ctx, const char *fmt, va_list ap);
int samu_printf(struct samu_ctx *ctx, const char *fmt, ...)
MUON_ATTR_FORMAT(printf, 2, 3);
void samu_puts(struct samu_ctx *ctx, const char *str);
void samu_puts_no_newline(struct samu_ctx *ctx, const char *str);
void samu_putchar(struct samu_ctx *ctx, const char c);

void *samu_xmalloc(struct arena *a, size_t);
void *samu_xreallocarray(struct arena *a, void *, size_t old, size_t new, size_t item_size);
char *samu_xmemdup(struct arena *a, const char *, size_t);
int samu_xasprintf(struct arena *a, char **, const char *, ...);

/* append a byte to a buffer */
void samu_bufadd(struct arena *a, struct samu_buffer *buf, char c);

/* allocates a new string with length n. n + 1 bytes are allocated for
 * s, but not initialized. */
struct samu_string *samu_mkstr(struct arena *a, size_t n);

/* canonicalizes the given path by removing duplicate slashes, and
 * folding '/.' and 'foo/..' */
void samu_canonpath(struct samu_string *);
/* make a directory (or parent directory of a file) recursively */
int samu_makedirs(struct samu_ctx *ctx, struct samu_string *, _Bool);
/* write a new file with the given name and contents */
int samu_writefile(const char *, struct samu_string *);
#endif
