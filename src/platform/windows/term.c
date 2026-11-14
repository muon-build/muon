/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <io.h>
#include "platform/windows/windows_min.h"
#include "platform/windows/windows_console.h"

#include "platform/term.h"

bool
term_winsize(struct workspace *wk, int fd, uint32_t *height, uint32_t *width)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	HANDLE h;
	DWORD mode;

	*height = 24;
	*width = 80;

	/* if not a console, or a terminal using conpty, use default values */
	h = (HANDLE *)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		return true;
	}
	if (!GetConsoleMode(h, &mode)) {
		return true;
	}

	/* otherwise, retrieve the geometry */
	if (GetConsoleScreenBufferInfo(h, &csbi)) {
		*height = csbi.dwSize.Y;
		*width = csbi.dwSize.X;
		return true;
	}

	return false;
}
