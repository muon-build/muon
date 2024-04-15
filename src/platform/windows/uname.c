/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "platform/uname.h"

bool
uname_sysname(const char **res)
{
	*res = "windows";
	return true;
}

bool
uname_machine(const char **res)
{
	SYSTEM_INFO si;

	GetSystemInfo(&si);
	switch (si.wProcessorArchitecture) {
	case PROCESSOR_ARCHITECTURE_AMD64: *res = "x86_64"; return true;
	case PROCESSOR_ARCHITECTURE_ARM: *res = "arm"; return true;
	case PROCESSOR_ARCHITECTURE_ARM64: *res = "aarch64"; return true;
	case PROCESSOR_ARCHITECTURE_IA64: *res = "ia64"; return true;
	case PROCESSOR_ARCHITECTURE_INTEL: *res = "i686"; return true;
	case PROCESSOR_ARCHITECTURE_UNKNOWN:
	/* fall through */
	default: return false;
	}
}
