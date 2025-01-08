/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_EMBEDDED_H
#define MUON_EMBEDDED_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/source.h"

struct embedded_file {
	const char *name;
	struct source src;
};

bool embedded_get(const char *name, struct source *src);
const struct embedded_file *embedded_file_list(uint32_t *len);
#endif
