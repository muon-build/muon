/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_PARSE_H
#define MUON_EXTERNAL_SAMU_PARSE_H

struct samu_environment;
struct samu_node;

void samu_parseinit(struct samu_ctx *ctx);
void samu_parse(struct samu_ctx *ctx, const char *name, struct samu_environment *env);

/* execute a function with all default nodes */
void samu_defaultnodes(struct samu_ctx *ctx, void fn(struct samu_ctx *ctx, struct samu_node *));

#endif
