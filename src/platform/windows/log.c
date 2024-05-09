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
#include "platform/assert.h"
#include "platform/log.h"
#include "platform/windows/log.h"

static const WORD color_map[] = { [1] = FOREGROUND_INTENSITY,
	[31] = FOREGROUND_RED,
	[32] = FOREGROUND_GREEN,
	[33] = FOREGROUND_GREEN | FOREGROUND_RED,
	[34] = FOREGROUND_BLUE,
	[35] = FOREGROUND_BLUE | FOREGROUND_RED,
	[36] = FOREGROUND_BLUE | FOREGROUND_GREEN,
	[37] = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED };

void
print_colorized(FILE *out, const char *s)
{
	if (tty_is_pty) {
		fwrite(s, 1, strlen(s), out);
	} else {
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
		WORD saved_attributes;

		/* Save current attributes */
		GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
		saved_attributes = consoleInfo.wAttributes;

		bool parsing_esc = false;
		const char *start = s;
		uint32_t len = 0;

		uint32_t esc_num = 0;
		for (; *s; ++s) {
			WORD attr = 0;
			if (*s == '\033') {
				attr = 0;
				if (len) {
					fwrite(start, 1, len, out);
					len = 0;
				}

				parsing_esc = true;
				esc_num = 0;
			} else if (parsing_esc) {
				if (*s == 'm' || *s == ';') {
					if (*s == 'm') {
						parsing_esc = false;
						start = s + 1;
					}

					assert(esc_num < ARRAY_LEN(color_map) && "esc_num out of range");
					attr = esc_num ? attr | color_map[esc_num] : saved_attributes;
					SetConsoleTextAttribute(hConsole, attr);
					esc_num = 0;
				} else if ('0' <= *s && *s <= '9') {
					esc_num *= 10;
					esc_num += (*s - '0');
				} else if (*s == '[') {
					// nothing
				} else {
					assert(false && "invalid character");
				}
			} else {
				++len;
			}
		}

		if (len) {
			fwrite(start, 1, len, out);
		}
	}
}
