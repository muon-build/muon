/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "platform/term.h"
#include "log.h"

bool
term_winsize(int fd, uint32_t *height, uint32_t *width)
{
	*height = 24;
	*width = 80;

	if (!term_isterm(fd)) {
		return true;
	}

	struct winsize w = { 0 };
	if (ioctl(fd, TIOCGWINSZ, &w) == -1) {
		return false;
	}

	if (w.ws_row) {
		*height = w.ws_row;
	}
	if (w.ws_col) {
		*width = w.ws_col;
	}
	return true;
}

bool
term_isterm(int fd)
{
	return isatty(fd) == 1;
}
