/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#ifdef _WIN32
#include <process.h>
#else
#include <stdlib.h>
#endif

#include "log.h"

#include "platform/assert.h"

MUON_NORETURN void muon_assert_fail(const char *msg, const char *file, uint32_t line, const char *func)
{
	LOG_E("%s:%d %s assertion failed: %s", file, line, func, msg);
	abort();
}
