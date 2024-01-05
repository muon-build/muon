/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_ENV_H
#define MUON_EXTERNAL_SAMU_ENV_H

struct samu_edge;
struct samu_evalstring;
struct samu_string;

void samu_envinit(struct samu_ctx *ctx);

/* create a new environment with an optional parent */
struct samu_environment *samu_mkenv(struct samu_ctx *ctx, struct samu_environment *);
/* search environment and its parents for a variable, returning the value or NULL if not found */
struct samu_string *samu_envvar(struct samu_environment *, char *);
/* add to environment a variable and its value, replacing the old value if there is one */
void samu_envaddvar(struct samu_ctx *ctx, struct samu_environment *env, char *var, struct samu_string *val);
/* evaluate an unevaluated string within an environment, returning the result */
struct samu_string *samu_enveval(struct samu_ctx *ctx, struct samu_environment *, struct samu_evalstring *);
/* search an environment and its parents for a rule, returning the rule or NULL if not found */
struct samu_rule *samu_envrule(struct samu_environment *env, char *name);
/* add a rule to an environment, or fail if the rule already exists */
void samu_envaddrule(struct samu_ctx *ctx, struct samu_environment *env, struct samu_rule *r);

/* create a new rule with the given name */
struct samu_rule *samu_mkrule(struct samu_ctx *ctx, char *);
/* add to rule a variable and its value */
void samu_ruleaddvar(struct samu_ctx *ctx, struct samu_rule *r, char *var, struct samu_evalstring *val);

/* create a new pool with the given name */
struct samu_pool *samu_mkpool(struct samu_ctx *ctx, char *name);
/* lookup a pool by name, or fail if it does not exist */
struct samu_pool *samu_poolget(struct samu_ctx *ctx, char *name);

/* evaluate and return an edge's variable, optionally shell-escaped */
struct samu_string *samu_edgevar(struct samu_ctx *ctx, struct samu_edge *e, char *var, bool escape);

#endif
