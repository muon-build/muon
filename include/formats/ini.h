/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FORMATS_INI_H
#define MUON_FORMATS_INI_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/source.h"

typedef bool((*inihcb)(void *ctx,
	struct source *src,
	const char *sect,
	const char *k,
	const char *v,
	struct source_location location));

struct workspace;

bool ini_parse(struct workspace *a, const char *path, struct source *src, char **buf, inihcb cb, void *octx);
bool ini_reparse(struct workspace *a, const char *path, const struct source *src, char *buf, inihcb cb, void *octx);
bool keyval_parse(struct workspace *a, const char *path, struct source *src, char **buf, inihcb cb, void *octx);
#endif
