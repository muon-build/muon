/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_ASSERT_H
#  define MUON_ASSERT_H

#  ifdef _WIN32
#    define assert(x) ((void)((x) || (win_assert_fail(#x, __FILE__, __LINE__, __func__),0)))
__declspec(noreturn) void win_assert_fail(const char *msg, const char *file, uint32_t line, const char *func);
#  else
#    include <assert.h>
#  endif
#endif
