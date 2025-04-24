/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_UNAME_H
#define MUON_PLATFORM_UNAME_H

#include <stdbool.h>

enum endianness {
	endianness_uninitialized,
	big_endian,
	little_endian,
};

const char *uname_sysname(void);
const char *uname_machine(void);
enum endianness uname_endian(void);
#endif
