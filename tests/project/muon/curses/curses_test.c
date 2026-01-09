/*
 * SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <curses.h>
#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
	// Initialize ncurses in a way that doesn't require a terminal
	// Just verify that the library links and basic functions are available
	
	// Check if we can call initscr (but don't actually do it since we may not have a terminal)
	// Instead, just verify the symbols exist by taking their addresses
	void *init_ptr = (void *)initscr;
	void *endwin_ptr = (void *)endwin;
	void *printw_ptr = (void *)printw;
	void *refresh_ptr = (void *)refresh;

	if (!init_ptr || !endwin_ptr || !printw_ptr || !refresh_ptr) {
		fprintf(stderr, "Failed to resolve ncurses symbols\n");
		return 1;
	}

	// Verify we can access curses constants
	int color_pairs = COLOR_PAIRS;
	int colors = COLORS;
	(void)color_pairs;
	(void)colors;

	return 0;
}