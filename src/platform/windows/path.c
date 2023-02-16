/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <string.h>
#include <stdbool.h>

/*
 * reference:
 * https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file#paths
 */
bool
path_is_absolute(const char *path)
{
	size_t length;

	if (!path || !*path) {
		return false;
	}

	/* \file.txt or \directory, rarely used */
	if (*path == '\\') {
		return true;
	}

	length = strlen(path);
	if (length < 3) {
		return false;
	}

	/* c:/ or c:\ case insensitive*/
	return
		(((path[0] >= 'a') && (path[0] <= 'z')) || ((path[0] >= 'A') && (path[0] <= 'Z'))) &&
		(path[1] == ':') &&
		((path[2] == '/') || (path[2] == '\\'));
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
