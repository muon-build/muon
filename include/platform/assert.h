/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_ASSERT_H
#define MUON_ASSERT_H

#include "compat.h"

#include <stdint.h>

MUON_NORETURN void muon_assert_fail(const char *msg, const char *file, uint32_t line, const char *func);

#define assert(x) ((void)((x) || (muon_assert_fail(#x, __FILE__, __LINE__, __func__), 0)))

#endif
