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

const char *
uname_sysname(void)
{
	return "windows";
}

const char *
uname_machine(void)
{
	SYSTEM_INFO si;

	GetSystemInfo(&si);
	switch (si.wProcessorArchitecture) {
	case PROCESSOR_ARCHITECTURE_AMD64: return "x86_64";
	case PROCESSOR_ARCHITECTURE_ARM: return "arm";
	case PROCESSOR_ARCHITECTURE_ARM64: return "aarch64";
	case PROCESSOR_ARCHITECTURE_IA64: return "ia64";
	case PROCESSOR_ARCHITECTURE_INTEL: return "i686";
	case PROCESSOR_ARCHITECTURE_UNKNOWN:
	/* fall through */
	default: return "unknown";
	}
}
