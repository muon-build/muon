/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_BACKTRACE_H
#define MUON_PLATFORM_BACKTRACE_H

#include <stddef.h>

#include "datastructures/arr.h"

struct platform_backtrace_frame {
	void *addr, *symbol;
	const char *symbol_name, *file_name;
	ptrdiff_t offset;
};

struct platform_backtrace {
	struct arr frames;
};

void platform_backtrace_capture(struct arena *a, struct platform_backtrace *bt);

extern bool have_platform_backtrace_capture;
#endif
