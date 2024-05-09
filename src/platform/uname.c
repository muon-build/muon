/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "platform/uname.h"

#include <stdint.h>

enum endianness
uname_endian(void)
{
	const uint32_t x = 1;
	if (((char *)&x)[0] == 1) {
		return little_endian;
	} else {
		return big_endian;
	}
}
