/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform/log.h"
#include "formats/ansi.h"

static void
print_colorized_flush_cb(void *_ctx, const struct str *s, uint32_t start, uint32_t len)
{
	FILE *out = _ctx;
	fwrite(&s->s[start], 1, len, out);
}

void
print_colorized(FILE *out, const char *s, uint32_t len, bool strip)
{
	if (!strip) {
		fwrite(s, 1, len, out);
		return;
	}

	parse_ansi(&(struct str) { s, len }, out, print_colorized_flush_cb, 0);
}
