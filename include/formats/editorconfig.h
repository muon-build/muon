/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FORMATS_EDITORCONFIG_H
#define MUON_FORMATS_EDITORCONFIG_H

#include "platform/filesystem.h"

struct editorconfig_opts {
	const char *indent_by;
};

void try_parse_editorconfig(struct source *src, struct editorconfig_opts *opts);
#endif
