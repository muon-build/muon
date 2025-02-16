/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_DEPS_H
#define MUON_EXTERNAL_SAMU_DEPS_H

struct samu_edge;

void samu_depsinit(struct samu_ctx *ctx, const char *builddir);
void samu_depsclose(struct samu_ctx *ctx);
void samu_depsload(struct samu_ctx *ctx, struct samu_edge *e);
void samu_depsrecord(struct samu_ctx *ctx, struct tstr *output, const char **filtered_output, struct samu_edge *e);

#endif
