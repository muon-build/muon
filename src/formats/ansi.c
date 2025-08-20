/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "formats/ansi.h"
#include "platform/assert.h"

void
parse_ansi(const struct str *s, void *usr_ctx, parse_ansi_flush_cb flush_cb, parse_ansi_attr_cb attr_cb)
{
	bool parsing_esc = false;
	uint32_t i, start = 0, len = 0, esc_num = 0;

	for (i = 0; i < s->len; ++i) {
		const char c = s->s[i];

		if (!parsing_esc && c == '\033') {
			if (len) {
				flush_cb(usr_ctx, s, start, len);
				len = 0;
			}
			parsing_esc = true;
		} else if (parsing_esc) {
			if (c == 'm' || c == ';') {
				if (c == 'm') {
					parsing_esc = false;
					start = i + 1;
				}

				if (attr_cb) {
					attr_cb(usr_ctx, esc_num);
				}
				esc_num = 0;
			} else if ('0' <= c && c <= '9') {
				esc_num *= 10;
				esc_num += (c - '0');
			} else if (c == '[') {
				// nothing
			} else {
				assert(false && "invalid character in ansi escape");
			}
		} else {
			++len;
		}
	}

	if (len) {
		flush_cb(usr_ctx, s, start, len);
	}
}
