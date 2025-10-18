/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_SCAN_H
#define MUON_EXTERNAL_SAMU_SCAN_H

void samu_scaninit(struct samu_ctx *ctx, struct samu_scanner *s, const char *path);

void samu_scanerror(struct samu_scanner *, const char *, ...)
MUON_ATTR_FORMAT(printf, 2, 3);
int samu_scankeyword(struct samu_ctx *ctx, struct samu_scanner *s, char **var);
char *samu_scanname(struct samu_ctx *ctx, struct samu_scanner *s);
struct samu_evalstring *samu_scanstring(struct samu_ctx *ctx, struct samu_scanner *s, bool path);
void samu_scanpaths(struct samu_ctx *ctx, struct samu_scanner *s);
void samu_scanchar(struct samu_scanner *, int);
int samu_scanpipe(struct samu_scanner *, int);
_Bool samu_scanindent(struct samu_scanner *);
void samu_scannewline(struct samu_scanner *);

#endif
