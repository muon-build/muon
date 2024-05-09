/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <process.h>

#include "log.h"
#include "platform/assert.h"

#ifdef _WIN32
__declspec(noreturn)
void
win_assert_fail(const char *msg, const char *file, uint32_t line, const char *func)
{
	LOG_E("%s:%d %s: %s", file, line, func, msg);
	abort();
}
#endif
