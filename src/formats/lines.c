/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include <string.h>

#include "formats/lines.h"

void
each_line(char *buf, uint64_t len, void *ctx, each_line_callback cb)
{
	char *line, *b;

	line = buf;

	while ((b = strchr(line, '\n'))) {
		*b = '\0';

		if (cb(ctx, line, b - line) != ir_cont) {
			return;
		}

		line = b + 1;

		if ((size_t)(line - buf) >= len) {
			return;
		}
	}

	if (*line) {
		cb(ctx, line, strlen(line));
	}
}

