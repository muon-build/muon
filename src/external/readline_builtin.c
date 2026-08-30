/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdio.h>
#include <string.h>

#include "external/readline.h"
#include "log.h"

char *
muon_readline(const char *prompt)
{
	static char buf[2048];

	log_raw("%s\n", prompt);
	if (!fgets(buf, 2048, stdin)) {
		return NULL;
	}

	size_t len = strlen(buf);
	while (len && strchr(" \n", buf[len - 1])) {
		--len;
	}
	buf[len] = 0;

	return buf;
}

int
muon_readline_history_add(const char *line)
{
	return 0;
}

void
muon_readline_history_free(void)
{
}
