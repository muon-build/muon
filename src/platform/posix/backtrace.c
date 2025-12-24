/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

// dladdr requires _GNU_SOURCE or _BSD_SOURCE
#undef _POSIX_C_SOURCE
#define _BSD_SOURCE

#include <dlfcn.h>
#include <execinfo.h>

#include "buf_size.h"
#include "platform/backtrace.h"

bool have_platform_backtrace_capture = true;

static void
platform_backtrace_resolve(struct platform_backtrace *bt)
{
	for (uint32_t i = 0; i < bt->frames.len; i++) {
		struct platform_backtrace_frame *frame = arr_get(&bt->frames, i);
		Dl_info info = { 0 };
		if (dladdr(frame->addr, &info) != 0) {
			frame->symbol_name = info.dli_sname;
			frame->file_name = info.dli_fname;
			frame->symbol = info.dli_saddr ? info.dli_saddr : frame->addr;
			frame->offset = (char *)frame->addr - (char *)frame->symbol;
		}
	}
}

void
platform_backtrace_capture(struct arena *a, struct platform_backtrace *bt)
{
	*bt = (struct platform_backtrace){ 0 };

	arr_init(a, &bt->frames, 32, struct platform_backtrace_frame);

	void *symbuf[128] = { 0 };
	uint32_t len = backtrace(symbuf, ARRAY_LEN(symbuf));

	for (uint32_t i = 0; i < len; i++) {
		arr_push(a, &bt->frames, &(struct platform_backtrace_frame){ .addr = symbuf[i] });
	}
	platform_backtrace_resolve(bt);
}
