/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "buf_size.h"
#include "formats/ansi.h"
#include "platform/assert.h"
#include "platform/log.h"
#include "platform/windows/log.h"

static const WORD color_map[] = {
	[1] = FOREGROUND_INTENSITY,
	[31] = FOREGROUND_RED,
	[32] = FOREGROUND_GREEN,
	[33] = FOREGROUND_GREEN | FOREGROUND_RED,
	[34] = FOREGROUND_BLUE,
	[35] = FOREGROUND_BLUE | FOREGROUND_RED,
	[36] = FOREGROUND_BLUE | FOREGROUND_GREEN,
	[37] = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED,
};

struct print_colorized_ctx {
	FILE *out;
	HANDLE console;
	WORD old_attr;
};

static void
print_colorized_flush_cb(void *_ctx, const struct str *s, uint32_t start, uint32_t len)
{
	FILE *out = _ctx;
	fwrite(&s->s[start], 1, len, out);
}

static void
print_colorized_attr_cb(void *_ctx, enum ansi_attr attr)
{
	struct print_colorized_ctx *ctx = _ctx;

	WORD attr_to_set;
	if (attr < ARRAY_LEN(color_map)) {
		attr_to_set = color_map[attr];
	}

	if (!attr_to_set) {
		attr_to_set = ctx->old_attr;
	}

	SetConsoleTextAttribute(ctx->console, attr_to_set);
}

void
print_colorized(FILE *out, const char *s, uint32_t len, bool strip)
{
	if (tty_is_pty) {
		fwrite(s, 1, len, out);
		return;
	}

	struct print_colorized_ctx ctx = {
		.out = out,
		.console = GetStdHandle(STD_OUTPUT_HANDLE),
	};

	{
		/* Save current attributes */
		CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
		GetConsoleScreenBufferInfo(ctx.console, &consoleInfo);
		ctx.old_attr = consoleInfo.wAttributes;
	}

	parse_ansi(&(struct str) { s, len }, out, print_colorized_flush_cb, print_colorized_attr_cb);
}
