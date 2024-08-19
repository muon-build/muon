/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FORMATS_EDITORCONFIG_H
#define MUON_FORMATS_EDITORCONFIG_H

#include "platform/filesystem.h"

struct fmt_opts;

bool editorconfig_pattern_match(const char *pattern, const char *string);

void try_parse_editorconfig(struct source *src, struct fmt_opts *opts);
#endif
