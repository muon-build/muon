/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_LOG_H
#define MUON_EXTERNAL_SAMU_LOG_H

struct samu_node;

void samu_loginit(struct samu_ctx *ctx, const char *);
void samu_logclose(struct samu_ctx *ctx);
void samu_logrecord(struct samu_ctx *ctx, struct samu_node *);

#endif
