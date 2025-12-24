/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "platform/backtrace.h"

bool have_platform_backtrace_capture = false;

void
platform_backtrace_capture(struct arena *a, struct platform_backtrace *bt)
{
}
