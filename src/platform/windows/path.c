/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdbool.h>
#include <string.h>

#include "lang/string.h"
#include "log.h"
#include "platform/path.h"

/*
 * reference:
 * https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file#paths
 */
bool
path_is_absolute(const char *path)
{
	if (!path || !*path) {
		return false;
	}

	/* \file.txt or \directory, rarely used */
	if (*path == '\\') {
		return true;
	}

	// Handle unix paths as well
	if (*path == '/') {
		return true;
	}

	return path_begins_with_win32_drive(path);
}

bool
path_is_basename(const char *path)
{
	const char *iter = path;

	while (*iter) {
		if ((*iter == '/') || (*iter == '\\')) {
			return false;
		}
		iter++;
	}

	return true;
}

void
path_to_posix(char *path)
{
	char *iter = path;
	while (*iter) {
		if (*iter == '\\') {
			*iter = '/';
		}
		iter++;
	}
}
