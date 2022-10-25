/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include "buf_size.h"
#include "platform/uname.h"

#include <stdint.h>
#include <string.h>
#include <sys/utsname.h>

static struct {
	struct utsname uname;
	char machine[BUF_SIZE_2k + 1],
	     sysname[BUF_SIZE_2k + 1];
	bool init;
} uname_info;

static void
strncpy_lowered(char *dest, const char *src, uint32_t len)
{
	uint32_t i;
	char c;

	for (i = 0; i < len && src[i]; ++i) {
		c = src[i];
		if ('A' <= c && c <= 'Z') {
			c = (c - 'A') + 'a';
		}

		dest[i] = c;
	}
}

static bool
uname_init(void)
{
	if (uname_info.init) {
		return true;
	}

	if (uname(&uname_info.uname) == -1) {
		return false;
	}

	strncpy_lowered(uname_info.machine, uname_info.uname.machine, BUF_SIZE_2k);
	strncpy_lowered(uname_info.sysname, uname_info.uname.sysname, BUF_SIZE_2k);

	uname_info.init = true;
	return true;
}

bool
uname_sysname(const char **res)
{
	if (!uname_init()) {
		return false;
	}

	*res = uname_info.sysname;
	return true;
}

bool
uname_machine(const char **res)
{
	if (!uname_init()) {
		return false;
	}

	*res = uname_info.machine;
	return true;
}

bool
uname_endian(enum endianness *res)
{
	const uint32_t x = 1;
	if (((char *)&x)[0] == 1) {
		*res = little_endian;
	} else {
		*res = big_endian;
	}

	return true;
}
