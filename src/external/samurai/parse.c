/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "external/samurai/ctx.h"

#include "external/samurai/env.h"
#include "external/samurai/graph.h"
#include "external/samurai/parse.h"
#include "external/samurai/scan.h"
#include "external/samurai/util.h"

void
samu_parseinit(struct samu_ctx *ctx)
{
	ctx->parse.deftarg = NULL;
	ctx->parse.ndeftarg = 0;
}

static void
samu_parselet(struct samu_ctx *ctx, struct samu_scanner *s, struct samu_evalstring **val)
{
	samu_scanchar(s, '=');
	*val = samu_scanstring(ctx, s, false);
	samu_scannewline(s);
}

static void
samu_parserule(struct samu_ctx *ctx, struct samu_scanner *s, struct samu_environment *env)
{
	struct samu_rule *r;
	char *var;
	struct samu_evalstring *val;
	bool hascommand = false, hasrspfile = false, hasrspcontent = false;

	r = samu_mkrule(ctx, samu_scanname(ctx, s));
	samu_scannewline(s);
	while (samu_scanindent(s)) {
		var = samu_scanname(ctx, s);
		samu_parselet(ctx, s, &val);
		samu_ruleaddvar(ctx, r, var, val);
		if (!val)
			continue;
		if (strcmp(var, "command") == 0)
			hascommand = true;
		else if (strcmp(var, "rspfile") == 0)
			hasrspfile = true;
		else if (strcmp(var, "rspfile_content") == 0)
			hasrspcontent = true;
	}
	if (!hascommand)
		samu_fatal("rule '%s' has no command", r->name);
	if (hasrspfile != hasrspcontent)
		samu_fatal("rule '%s' has rspfile and no rspfile_content or vice versa", r->name);
	samu_envaddrule(ctx, env, r);
}

static void
samu_parseedge(struct samu_ctx *ctx, struct samu_scanner *s, struct samu_environment *env)
{
	struct samu_edge *e;
	struct samu_evalstring *str, **path;
	char *name;
	struct samu_string *val;
	struct samu_node *n;
	size_t i;
	int p;

	e = samu_mkedge(ctx, env);

	samu_scanpaths(ctx, s);
	e->outimpidx = ctx->scan.npaths;
	if (samu_scanpipe(s, 1))
		samu_scanpaths(ctx, s);
	e->nout = ctx->scan.npaths;
	if (e->nout == 0)
		samu_scanerror(s, "expected output path");
	samu_scanchar(s, ':');
	name = samu_scanname(ctx, s);
	e->rule = samu_envrule(env, name);
	if (!e->rule)
		samu_fatal("undefined rule '%s'", name);
	samu_scanpaths(ctx, s);
	e->inimpidx = ctx->scan.npaths - e->nout;
	p = samu_scanpipe(s, 1 | 2);
	if (p == 1) {
		samu_scanpaths(ctx, s);
		p = samu_scanpipe(s, 2);
	}
	e->inorderidx = ctx->scan.npaths - e->nout;
	if (p == 2)
		samu_scanpaths(ctx, s);
	e->nin = ctx->scan.npaths - e->nout;
	samu_scannewline(s);
	while (samu_scanindent(s)) {
		name = samu_scanname(ctx, s);
		samu_parselet(ctx, s, &str);
		val = samu_enveval(ctx, env, str);
		samu_envaddvar(ctx, e->env, name, val);
	}

	e->out = samu_xreallocarray(&ctx->arena, NULL, 0, e->nout, sizeof(e->out[0]));
	for (i = 0, path = ctx->scan.paths; i < e->nout; ++path) {
		val = samu_enveval(ctx, e->env, *path);
		samu_canonpath(val);
		n = samu_mknode(ctx, val);
		if (n->gen) {
			if (!ctx->parseopts.dupbuildwarn)
				samu_fatal("multiple rules generate '%s'", n->path->s);
			samu_warn("multiple rules generate '%s'", n->path->s);
			--e->nout;
			if (i < e->outimpidx)
				--e->outimpidx;
		} else {
			n->gen = e;
			e->out[i] = n;
			++i;
		}
	}
	e->in = samu_xreallocarray(&ctx->arena, NULL, 0, e->nin, sizeof(e->in[0]));
	for (i = 0; i < e->nin; ++i, ++path) {
		val = samu_enveval(ctx, e->env, *path);
		samu_canonpath(val);
		n = samu_mknode(ctx, val);
		e->in[i] = n;
		samu_nodeuse(ctx, n, e);
	}
	ctx->scan.npaths = 0;

	val = samu_edgevar(ctx, e, "pool", true);
	if (val)
		e->pool = samu_poolget(ctx, val->s);
}

static void
samu_parseinclude(struct samu_ctx *ctx, struct samu_scanner *s, struct samu_environment *env, bool newscope)
{
	struct samu_evalstring *str;
	struct samu_string *path;

	str = samu_scanstring(ctx, s, true);
	if (!str)
		samu_scanerror(s, "expected include path");
	samu_scannewline(s);
	path = samu_enveval(ctx, env, str);

	if (newscope)
		env = samu_mkenv(ctx, env);
	samu_parse(ctx, path->s, env);
}

static void
samu_parsedefault(struct samu_ctx *ctx, struct samu_scanner *s, struct samu_environment *env)
{
	struct samu_string *path;
	struct samu_node *n;
	size_t i;

	samu_scanpaths(ctx, s);
	ctx->parse.deftarg = samu_xreallocarray(&ctx->arena, ctx->parse.deftarg, ctx->parse.ndeftarg, ctx->parse.ndeftarg + ctx->scan.npaths, sizeof(*ctx->parse.deftarg));
	for (i = 0; i < ctx->scan.npaths; ++i) {
		path = samu_enveval(ctx, env, ctx->scan.paths[i]);
		samu_canonpath(path);
		n = samu_nodeget(ctx, path->s, path->n);
		if (!n)
			samu_fatal("unknown target '%s'", path->s);
		ctx->parse.deftarg[ctx->parse.ndeftarg++] = n;
	}
	samu_scannewline(s);
	ctx->scan.npaths = 0;
}

static void
samu_parsepool(struct samu_ctx *ctx, struct samu_scanner *s, struct samu_environment *env)
{
	struct samu_pool *p;
	struct samu_evalstring *val;
	struct samu_string *str;
	char *var, *end;

	p = samu_mkpool(ctx, samu_scanname(ctx, s));
	samu_scannewline(s);
	while (samu_scanindent(s)) {
		var = samu_scanname(ctx, s);
		samu_parselet(ctx, s, &val);
		if (strcmp(var, "depth") == 0) {
			str = samu_enveval(ctx, env, val);
			p->maxjobs = strtol(str->s, &end, 10);
			if (*end)
				samu_fatal("invalid pool depth '%s'", str->s);
		} else {
			samu_fatal("unexpected pool variable '%s'", var);
		}
	}
	if (!p->maxjobs)
		samu_fatal("pool '%s' has no depth", p->name);
}

static void
samu_checkversion(const char *ver)
{
	int major, minor = 0;

	if (sscanf(ver, "%d.%d", &major, &minor) < 1)
		samu_fatal("invalid ninja_required_version");
	if (major > ninjamajor || (major == ninjamajor && minor > ninjaminor))
		samu_fatal("ninja_required_version %s is newer than %d.%d", ver, ninjamajor, ninjaminor);
}

void
samu_parse(struct samu_ctx *ctx, const char *name, struct samu_environment *env)
{
	struct samu_scanner s;
	char *var;
	struct samu_string *val;
	struct samu_evalstring *str;

	samu_scaninit(&s, name);
	for (;;) {
		switch (samu_scankeyword(ctx, &s, &var)) {
		case SAMU_RULE:
			samu_parserule(ctx, &s, env);
			break;
		case SAMU_BUILD:
			samu_parseedge(ctx, &s, env);
			break;
		case SAMU_INCLUDE:
			samu_parseinclude(ctx, &s, env, false);
			break;
		case SAMU_SUBNINJA:
			samu_parseinclude(ctx, &s, env, true);
			break;
		case SAMU_DEFAULT:
			samu_parsedefault(ctx, &s, env);
			break;
		case SAMU_POOL:
			samu_parsepool(ctx, &s, env);
			break;
		case SAMU_VARIABLE:
			samu_parselet(ctx, &s, &str);
			val = samu_enveval(ctx, env, str);
			if (strcmp(var, "ninja_required_version") == 0)
				samu_checkversion(val->s);
			samu_envaddvar(ctx, env, var, val);
			break;
		case EOF:
			samu_scanclose(&s);
			return;
		}
	}
}

void
samu_defaultnodes(struct samu_ctx *ctx, void fn(struct samu_ctx *ctx, struct samu_node *))
{
	struct samu_edge *e;
	struct samu_node *n;
	size_t i;

	if (ctx->parse.ndeftarg > 0) {
		for (i = 0; i < ctx->parse.ndeftarg; ++i)
			fn(ctx, ctx->parse.deftarg[i]);
	} else {
		/* by default build all nodes which are not used by any edges */
		for (e = ctx->graph.alledges; e; e = e->allnext) {
			for (i = 0; i < e->nout; ++i) {
				n = e->out[i];
				if (n->nuse == 0)
					fn(ctx, n);
			}
		}
	}
}
