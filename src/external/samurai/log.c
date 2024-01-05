/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "external/samurai/ctx.h"

#include "external/samurai/graph.h"
#include "external/samurai/log.h"
#include "external/samurai/util.h"

static const char *samu_logname = ".ninja_log";
static const char *samu_logtmpname = ".ninja_log.tmp";
static const char *samu_logfmt = "# ninja log v%d\n";
static const int samu_logver = 5;

static char *
samu_nextfield(char **end)
{
	char *s = *end;

	if (!*s) {
		samu_warn("corrupt build log: missing field");
		return NULL;
	}
	*end += strcspn(*end, "\t\n");
	if (**end)
		*(*end)++ = '\0';

	return s;
}

void
samu_loginit(struct samu_ctx *ctx, const char *builddir)
{
	int ver;
	char *logpath = (char *)samu_logname, *logtmppath = (char *)samu_logtmpname, *p, *s;
	size_t nline, nentry, i;
	struct samu_edge *e;
	struct samu_node *n;
	int64_t mtime;
	struct samu_buffer buf = {0};

	nline = 0;
	nentry = 0;

	if (ctx->log.logfile) {
		fclose(ctx->log.logfile);
		ctx->log.logfile = NULL;
	}
	if (builddir)
		samu_xasprintf(&ctx->arena, &logpath, "%s/%s", builddir, samu_logname);
	ctx->log.logfile = fopen(logpath, "r+");
	if (!ctx->log.logfile) {
		if (errno != ENOENT)
			samu_fatal("open %s:", logpath);
		goto rewrite;
	}
	setvbuf(ctx->log.logfile, NULL, _IOLBF, 0);
	if (fscanf(ctx->log.logfile, samu_logfmt, &ver) < 1)
		goto rewrite;
	if (ver != samu_logver)
		goto rewrite;

	for (;;) {
		if (buf.cap - buf.len < BUFSIZ) {
			size_t newcap = buf.cap ? buf.cap * 2 : BUFSIZ;
			buf.data = samu_xreallocarray(&ctx->arena, buf.data, buf.cap, newcap, 1);
			buf.cap = newcap;
		}
		buf.data[buf.cap - 2] = '\0';
		if (!fgets(buf.data + buf.len, buf.cap - buf.len, ctx->log.logfile))
			break;
		if (buf.data[buf.cap - 2] && buf.data[buf.cap - 2] != '\n') {
			buf.len = buf.cap - 1;
			continue;
		}
		++nline;
		p = buf.data;
		buf.len = 0;
		if (!samu_nextfield(&p))  /* start time */
			continue;
		if (!samu_nextfield(&p))  /* end time */
			continue;
		s = samu_nextfield(&p);  /* mtime (used for restat) */
		if (!s)
			continue;
		mtime = strtoll(s, &s, 10);
		if (*s) {
			samu_warn("corrupt build log: invalid mtime");
			continue;
		}
		s = samu_nextfield(&p);  /* output path */
		if (!s)
			continue;
		n = samu_nodeget(ctx, s, 0);
		if (!n || !n->gen)
			continue;
		if (n->logmtime == SAMU_MTIME_MISSING)
			++nentry;
		n->logmtime = mtime;
		s = samu_nextfield(&p);  /* command hash */
		if (!s)
			continue;
		n->hash = strtoull(s, &s, 16);
		if (*s) {
			samu_warn("corrupt build log: invalid hash for '%s'", n->path->s);
			continue;
		}
	}
	if (ferror(ctx->log.logfile)) {
		samu_warn("build log read:");
		goto rewrite;
	}
	if (nline <= 100 || nline <= 3 * nentry) {
		return;
	}

rewrite:
	if (ctx->log.logfile) {
		fclose(ctx->log.logfile);
		ctx->log.logfile = NULL;
	}
	if (builddir)
		samu_xasprintf(&ctx->arena, &logtmppath, "%s/%s", builddir, samu_logtmpname);
	ctx->log.logfile = fopen(logtmppath, "w");
	if (!ctx->log.logfile)
		samu_fatal("open %s:", logtmppath);
	setvbuf(ctx->log.logfile, NULL, _IOLBF, 0);
	fprintf(ctx->log.logfile, samu_logfmt, samu_logver);
	if (nentry > 0) {
		for (e = ctx->graph.alledges; e; e = e->allnext) {
			for (i = 0; i < e->nout; ++i) {
				n = e->out[i];
				if (!n->hash)
					continue;
				samu_logrecord(ctx, n);
			}
		}
	}
	fflush(ctx->log.logfile);
	if (ferror(ctx->log.logfile))
		samu_fatal("build log write failed");
	if (rename(logtmppath, logpath) < 0)
		samu_fatal("build log rename:");
}

void
samu_logclose(struct samu_ctx *ctx)
{
	fflush(ctx->log.logfile);
	if (ferror(ctx->log.logfile))
		samu_fatal("build log write failed");
	fclose(ctx->log.logfile);
	ctx->log.logfile = NULL;
}

void
samu_logrecord(struct samu_ctx *ctx, struct samu_node *n)
{
	fprintf(ctx->log.logfile, "0\t0\t%" PRId64 "\t%s\t%" PRIx64 "\n", n->logmtime, n->path->s, n->hash);
}
