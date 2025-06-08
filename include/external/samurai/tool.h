/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_TOOL_H
#define MUON_EXTERNAL_SAMU_TOOL_H

const struct samu_tool *samu_toolget(const char *);
void samu_toollist(struct samu_ctx *ctx);
#endif
