/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "external/samurai/ctx.h"

#include "external/samurai/env.h"
#include "external/samurai/graph.h"
#include "external/samurai/htab.h"
#include "external/samurai/util.h"

static void
samu_delnode(void *p)
{
}

void
samu_graphinit(struct samu_ctx *ctx)
{
	struct samu_edge *e;

	/* delete old nodes and edges in case we rebuilt the manifest */
	samu_delhtab(ctx->graph.allnodes, samu_delnode);
	while (ctx->graph.alledges) {
		e = ctx->graph.alledges;
		ctx->graph.alledges = e->allnext;
	}
	ctx->graph.allnodes = samu_mkhtab(&ctx->arena, 1024);
}

struct samu_node *
samu_mknode(struct samu_ctx *ctx, struct samu_string *path)
{
	void **v;
	struct samu_node *n;
	struct samu_hashtablekey k;

	samu_htabkey(&k, path->s, path->n);
	v = samu_htabput(&ctx->arena, ctx->graph.allnodes, &k);
	if (*v) {
		return *v;
	}
	n = samu_xmalloc(&ctx->arena, sizeof(*n));
	n->path = path;
	n->shellpath = NULL;
	n->gen = NULL;
	n->use = NULL;
	n->nuse = 0;
	n->mtime = SAMU_MTIME_UNKNOWN;
	n->logmtime = SAMU_MTIME_MISSING;
	n->hash = 0;
	n->id = -1;
	*v = n;

	return n;
}

struct samu_node *
samu_nodeget(struct samu_ctx *ctx, const char *path, size_t len)
{
	struct samu_hashtablekey k;

	if (!len)
		len = strlen(path);
	samu_htabkey(&k, path, len);
	return samu_htabget(ctx->graph.allnodes, &k);
}

void
samu_nodestat(struct samu_node *n)
{
	struct stat st;

	if (stat(n->path->s, &st) < 0) {
		if (errno != ENOENT)
			samu_fatal("stat %s:", n->path->s);
		n->mtime = SAMU_MTIME_MISSING;
	} else {
#ifdef __APPLE__
		n->mtime = (int64_t)st.st_mtime * 1000000000 + st.st_mtimensec;
/*
Illumos hides the members of st_mtim when you define _POSIX_C_SOURCE
since it has not been updated to support POSIX.1-2008:
https://www.illumos.org/issues/13327
*/
#elif defined(__sun) && !defined(__EXTENSIONS__)
		n->mtime = (int64_t)st.st_mtim.__tv_sec * 1000000000 + st.st_mtim.__tv_nsec;
#else
		n->mtime = (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
	}
}

struct samu_string *
samu_nodepath(struct samu_ctx *ctx, struct samu_node *n, bool escape)
{
	char *s, *d;
	int nquote;

	if (!escape)
		return n->path;
	if (n->shellpath)
		return n->shellpath;
	escape = false;
	nquote = 0;
	for (s = n->path->s; *s; ++s) {
		if (!isalnum(*(unsigned char *)s) && !strchr("_+-./", *s))
			escape = true;
		if (*s == '\'')
			++nquote;
	}
	if (escape) {
		n->shellpath = samu_mkstr(&ctx->arena, n->path->n + 2 + 3 * nquote);
		d = n->shellpath->s;
		*d++ = '\'';
		for (s = n->path->s; *s; ++s) {
			*d++ = *s;
			if (*s == '\'') {
				*d++ = '\\';
				*d++ = '\'';
				*d++ = '\'';
			}
		}
		*d++ = '\'';
	} else {
		n->shellpath = n->path;
	}
	return n->shellpath;
}

void
samu_nodeuse(struct samu_ctx *ctx, struct samu_node *n, struct samu_edge *e)
{
	/* allocate in powers of two */
	if (!(n->nuse & (n->nuse - 1))) {
		size_t new_nuse = n->nuse ? n->nuse * 2 : 1;
		n->use = samu_xreallocarray(&ctx->arena, n->use, n->nuse, new_nuse, sizeof(e));
	}
	n->use[n->nuse++] = e;
}

struct samu_edge *
samu_mkedge(struct samu_ctx *ctx, struct samu_environment *parent)
{
	struct samu_edge *e;

	e = samu_xmalloc(&ctx->arena, sizeof(*e));
	e->env = samu_mkenv(ctx, parent);
	e->pool = NULL;
	e->out = NULL;
	e->nout = 0;
	e->in = NULL;
	e->nin = 0;
	e->flags = 0;
	e->allnext = ctx->graph.alledges;
	ctx->graph.alledges = e;

	return e;
}

void
samu_edgehash(struct samu_ctx *ctx, struct samu_edge *e)
{
	static const char sep[] = ";rspfile=";
	struct samu_string *cmd, *rsp, *s;

	if (e->flags & FLAG_HASH)
		return;
	e->flags |= FLAG_HASH;
	cmd = samu_edgevar(ctx, e, "command", true);
	if (!cmd)
		samu_fatal("rule '%s' has no command", e->rule->name);
	rsp = samu_edgevar(ctx, e, "rspfile_content", true);
	if (rsp && rsp->n > 0) {
		s = samu_mkstr(&ctx->arena, cmd->n + sizeof(sep) - 1 + rsp->n);
		memcpy(s->s, cmd->s, cmd->n);
		memcpy(s->s + cmd->n, sep, sizeof(sep) - 1);
		memcpy(s->s + cmd->n + sizeof(sep) - 1, rsp->s, rsp->n);
		s->s[s->n] = '\0';
		e->hash = samu_murmurhash64a(s->s, s->n);
	} else {
		e->hash = samu_murmurhash64a(cmd->s, cmd->n);
	}
}

static struct samu_edge *
samu_mkphony(struct samu_ctx *ctx, struct samu_node *n)
{
	struct samu_edge *e;

	e = samu_mkedge(ctx, ctx->env.rootenv);
	e->rule = &ctx->phonyrule;
	e->inimpidx = 0;
	e->inorderidx = 0;
	e->outimpidx = 1;
	e->nout = 1;
	e->out = samu_xmalloc(&ctx->arena, sizeof(n));
	e->out[0] = n;

	return e;
}

void
samu_edgeadddeps(struct samu_ctx *ctx, struct samu_edge *e, struct samu_node **deps, size_t ndeps)
{
	struct samu_node **order, *n;
	size_t norder, i;

	for (i = 0; i < ndeps; ++i) {
		n = deps[i];
		if (!n->gen)
			n->gen = samu_mkphony(ctx, n);
		samu_nodeuse(ctx, n, e);
	}
	e->in = samu_xreallocarray(&ctx->arena, e->in, e->nin, e->nin + ndeps, sizeof(e->in[0]));
	order = e->in + e->inorderidx;
	norder = e->nin - e->inorderidx;
	memmove(order + ndeps, order, norder * sizeof(e->in[0]));
	memcpy(order, deps, ndeps * sizeof(e->in[0]));
	e->inorderidx += ndeps;
	e->nin += ndeps;
}
