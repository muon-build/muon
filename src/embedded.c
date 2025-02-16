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

#ifdef MUON_BOOTSTRAPPED
#include "embedded_files.h"
#else
static struct embedded_file embedded[] = { 0 };
static uint32_t embedded_len = 0;
#endif

bool
embedded_get(const char *name, struct source *src_out)
{
	bool bootstrapped = false;
#ifdef MUON_BOOTSTRAPPED
	bootstrapped = true;
#endif

	if (!bootstrapped) {
		TSTR_manual(path);
		path_dirname(0, &path, __FILE__);
		path_push(0, &path, "script");
		path_push(0, &path, name);
		struct source src = { 0 };
		if (!fs_file_exists(path.buf)) {
			return false;
		} else if (!fs_read_entire_file(path.buf, &src)) {
			return false;
		}
		*src_out = src;
		return true;
	}

	uint32_t i;
	for (i = 0; i < embedded_len; ++i) {
		if (strcmp(embedded[i].name, name) == 0) {
			*src_out = embedded[i].src;
			return true;
		}
	}

	return false;
}

const struct embedded_file *
embedded_file_list(uint32_t *len)
{
	*len = embedded_len;
	return embedded;
}
