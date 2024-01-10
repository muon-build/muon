/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "external/samurai/ctx.h"

#include "external/samurai/env.h"
#include "external/samurai/graph.h"
#include "external/samurai/tree.h"
#include "external/samurai/util.h"

struct samu_environment {
	struct samu_environment *parent;
	struct samu_treenode *bindings;
	struct samu_treenode *rules;
	struct samu_environment *allnext;
};

static void samu_addpool(struct samu_ctx *ctx, struct samu_pool *p);

void
samu_envinit(struct samu_ctx *ctx)
{
	struct samu_environment *env;

	/* free old environments and pools in case we rebuilt the manifest */
	while (ctx->env.allenvs) {
		env = ctx->env.allenvs;
		ctx->env.allenvs = env->allnext;
	}

	ctx->env.rootenv = samu_mkenv(ctx, NULL);
	samu_envaddrule(ctx, ctx->env.rootenv, &ctx->phonyrule);
	ctx->env.pools = NULL;
	samu_addpool(ctx, &ctx->consolepool);
}

static void
samu_addvar(struct samu_ctx *ctx, struct samu_treenode **tree, char *var, void *val)
{
	samu_treeinsert(ctx, tree, var, val);
}

struct samu_environment *
samu_mkenv(struct samu_ctx *ctx, struct samu_environment *parent)
{
	struct samu_environment *env;

	env = samu_xmalloc(&ctx->arena, sizeof(*env));
	env->parent = parent;
	env->bindings = NULL;
	env->rules = NULL;
	env->allnext = ctx->env.allenvs;
	ctx->env.allenvs = env;

	return env;
}

struct samu_string *
samu_envvar(struct samu_environment *env, char *var)
{
	struct samu_treenode *n;

	do {
		n = samu_treefind(env->bindings, var);
		if (n)
			return n->value;
		env = env->parent;
	} while (env);

	return NULL;
}

void
samu_envaddvar(struct samu_ctx *ctx, struct samu_environment *env, char *var, struct samu_string *val)
{
	samu_addvar(ctx, &env->bindings, var, val);
}

static struct samu_string *
samu_merge(struct samu_ctx *ctx, struct samu_evalstring *str, size_t n)
{
	struct samu_string *result;
	struct samu_evalstring *p;
	char *s;

	result = samu_mkstr(&ctx->arena, n);
	s = result->s;
	for (p = str; p; p = p->next) {
		if (!p->str)
			continue;
		memcpy(s, p->str->s, p->str->n);
		s += p->str->n;
	}
	*s = '\0';

	return result;
}

struct samu_string *
samu_enveval(struct samu_ctx *ctx, struct samu_environment *env, struct samu_evalstring *str)
{
	size_t n;
	struct samu_evalstring *p;
	struct samu_string *res;

	n = 0;
	for (p = str; p; p = p->next) {
		if (p->var)
			p->str = samu_envvar(env, p->var);
		if (p->str)
			n += p->str->n;
	}
	res = samu_merge(ctx, str, n);

	return res;
}

void
samu_envaddrule(struct samu_ctx *ctx, struct samu_environment *env, struct samu_rule *r)
{
	if (samu_treeinsert(ctx, &env->rules, r->name, r))
		samu_fatal("rule '%s' redefined", r->name);
}

struct samu_rule *
samu_envrule(struct samu_environment *env, char *name)
{
	struct samu_treenode *n;

	do {
		n = samu_treefind(env->rules, name);
		if (n)
			return n->value;
		env = env->parent;
	} while (env);

	return NULL;
}

static struct samu_string *
samu_pathlist(struct samu_ctx *ctx, struct samu_node **nodes, size_t n, char sep, bool escape)
{
	size_t i, len;
	struct samu_string *path, *result;
	char *s;

	if (n == 0)
		return NULL;
	if (n == 1)
		return samu_nodepath(ctx, nodes[0], escape);
	for (i = 0, len = 0; i < n; ++i)
		len += samu_nodepath(ctx, nodes[i], escape)->n;
	result = samu_mkstr(&ctx->arena, len + n - 1);
	s = result->s;
	for (i = 0; i < n; ++i) {
		path = samu_nodepath(ctx, nodes[i], escape);
		memcpy(s, path->s, path->n);
		s += path->n;
		*s++ = sep;
	}
	*--s = '\0';

	return result;
}

struct samu_rule *
samu_mkrule(struct samu_ctx *ctx, char *name)
{
	struct samu_rule *r;

	r = samu_xmalloc(&ctx->arena, sizeof(*r));
	r->name = name;
	r->bindings = NULL;

	return r;
}

void
samu_ruleaddvar(struct samu_ctx *ctx, struct samu_rule *r, char *var, struct samu_evalstring *val)
{
	samu_addvar(ctx, &r->bindings, var, val);
}

struct samu_string *
samu_edgevar(struct samu_ctx *ctx, struct samu_edge *e, char *var, bool escape)
{
	static void *const cycle = (void *)&cycle;
	struct samu_evalstring *str, *p;
	struct samu_treenode *n;
	size_t len;

	if (strcmp(var, "in") == 0)
		return samu_pathlist(ctx, e->in, e->inimpidx, ' ', escape);
	if (strcmp(var, "in_newline") == 0)
		return samu_pathlist(ctx, e->in, e->inimpidx, '\n', escape);
	if (strcmp(var, "out") == 0)
		return samu_pathlist(ctx, e->out, e->outimpidx, ' ', escape);
	n = samu_treefind(e->env->bindings, var);
	if (n)
		return n->value;
	n = samu_treefind(e->rule->bindings, var);
	if (!n)
		return samu_envvar(e->env->parent, var);
	if (n->value == cycle)
		samu_fatal("cycle in rule variable involving '%s'", var);
	str = n->value;
	n->value = cycle;
	len = 0;
	for (p = str; p; p = p->next) {
		if (p->var)
			p->str = samu_edgevar(ctx, e, p->var, escape);
		if (p->str)
			len += p->str->n;
	}
	n->value = str;
	return samu_merge(ctx, str, len);
}

static void
samu_addpool(struct samu_ctx *ctx, struct samu_pool *p)
{
	if (samu_treeinsert(ctx, &ctx->env.pools, p->name, p))
		samu_fatal("pool '%s' redefined", p->name);
}

struct samu_pool *
samu_mkpool(struct samu_ctx *ctx, char *name)
{
	struct samu_pool *p;

	p = samu_xmalloc(&ctx->arena, sizeof(*p));
	p->name = name;
	p->numjobs = 0;
	p->maxjobs = 0;
	p->work = NULL;
	samu_addpool(ctx, p);

	return p;
}

struct samu_pool *
samu_poolget(struct samu_ctx *ctx, char *name)
{
	struct samu_treenode *n;

	n = samu_treefind(ctx->env.pools, name);
	if (!n)
		samu_fatal("unknown pool '%s'", name);

	return n->value;
}
