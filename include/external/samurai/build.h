/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_BUILD_H
#define MUON_EXTERNAL_SAMU_BUILD_H

struct samu_node;

/* reset state, so a new build can be executed */
void samu_buildreset(struct samu_ctx *ctx);
/* schedule a particular target to be built */
void samu_buildadd(struct samu_ctx *ctx, struct samu_node *n);
/* execute rules to build the scheduled targets */
void samu_build(struct samu_ctx *ctx);

#endif
