/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdint.h>
#include <string.h>
#include <sys/utsname.h>

#include "buf_size.h"
#include "error.h"
#include "platform/assert.h"
#include "platform/uname.h"

static struct {
	struct utsname uname;
	char machine[BUF_SIZE_2k + 1], sysname[BUF_SIZE_2k + 1];
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

static void
uname_init(void)
{
	if (uname_info.init) {
		return;
	}

	if (uname(&uname_info.uname) == -1) {
		UNREACHABLE;
	}

	strncpy_lowered(uname_info.machine, uname_info.uname.machine, BUF_SIZE_2k);
	strncpy_lowered(uname_info.sysname, uname_info.uname.sysname, BUF_SIZE_2k);

	uname_info.init = true;
}

const char *
uname_sysname(void)
{
	uname_init();

	return uname_info.sysname;
}

const char *
uname_machine(void)
{
	uname_init();

	return uname_info.machine;
}
