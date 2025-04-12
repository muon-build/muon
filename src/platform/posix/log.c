/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform/log.h"

void
print_colorized(FILE *out, const char *s, bool strip)
{
	if (!strip) {
		fwrite(s, 1, strlen(s), out);
		return;
	}

	bool parsing_esc = false;
	const char *start = s;
	uint32_t len = 0;

	for (; *s; ++s) {
		if (*s == '\033') {
			if (len) {
				fwrite(start, 1, len, out);
				len = 0;
			}

			parsing_esc = true;
		} else if (parsing_esc) {
			if (*s == 'm') {
				parsing_esc = false;
				start = s + 1;
			}
		} else {
			++len;
		}
	}

	if (len) {
		fwrite(start, 1, len, out);
	}
}
