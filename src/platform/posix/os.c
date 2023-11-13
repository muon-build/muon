/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <unistd.h>

#include "platform/os.h"

bool os_chdir(const char *path)
{
	return chdir(path) == 0;
}

char *os_getcwd(char *buf, size_t size)
{
	return getcwd(buf, size);
}
