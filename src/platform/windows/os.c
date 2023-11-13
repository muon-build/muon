/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <errno.h>
#include <windows.h>

#include "platform/os.h"

bool os_chdir(const char *path)
{
	BOOL res;

	res = SetCurrentDirectory(path);
	if (!res) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND) {
			errno = ENOENT;
		}
		else if (GetLastError() == ERROR_PATH_NOT_FOUND) {
			errno = ENOTDIR;
		}
		else if (GetLastError() == ERROR_FILENAME_EXCED_RANGE) {
			errno = ENAMETOOLONG;
		} else {
			errno = EIO;
		}
	}

	return res;
}

char *os_getcwd(char *buf, size_t size)
{
	DWORD len;

        /* set errno to ERANGE for crossplatform usage of getcwd() in path.c */
	len = GetCurrentDirectory(0UL, NULL);
	if (size < len) {
		errno = ERANGE;
		return NULL;
	}

	len = GetCurrentDirectory(size, buf);
	if (!len) {
		errno = EPERM;
		return NULL;
	}

	return buf;
}
