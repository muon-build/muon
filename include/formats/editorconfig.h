/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FORMATS_EDITORCONFIG_H
#define MUON_FORMATS_EDITORCONFIG_H

#include "lang/source.h"

struct fmt_opts;

bool editorconfig_pattern_match(const char *pattern, const char *string);

struct arena;
struct workspace;
void try_parse_editorconfig(struct workspace *wk, struct source *src, struct fmt_opts *opts);
#endif
