/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include <string.h>
#include <stdint.h>

#include "embedded.h"

struct embedded_file {
	const char *name, *src;
};

#ifdef MUON_BOOTSTRAPPED
#include "embedded_files.h"
#else
static struct embedded_file embedded[] = { 0 };
static uint32_t embedded_len = 0;
#endif

const char *
embedded_get(const char *name)
{
	uint32_t i;
	for (i = 0; i < embedded_len; ++i) {
		if (strcmp(embedded[i].name, name) == 0) {
			return embedded[i].src;
		}
	}

	return NULL;
}
