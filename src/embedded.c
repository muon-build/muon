/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdint.h>
#include <string.h>

#include "embedded.h"
#include "lang/string.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

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
	bool bootstrapped = false;
#ifdef MUON_BOOTSTRAPPED
	bootstrapped = true;
#endif

	if (!bootstrapped) {
		SBUF_manual(path);
		path_dirname(0, &path, __FILE__);
		path_push(0, &path, "script");
		path_push(0, &path, name);
		struct source src = { 0 };
		if (!fs_file_exists(path.buf)) {
			return 0;
		} else if (!fs_read_entire_file(path.buf, &src)) {
			return 0;
		}
		return src.src;
	}

	uint32_t i;
	for (i = 0; i < embedded_len; ++i) {
		if (strcmp(embedded[i].name, name) == 0) {
			return embedded[i].src;
		}
	}

	return 0;
}
