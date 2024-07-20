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
	if (feof(stdin)) {
		return NULL;
	}

	fputs(prompt, log_file());
	fgets(buf, 2048, stdin);

	uint32_t len = strlen(buf);
	int32_t i;
	for (i = len - 1; i >= 0; --i) {
		if (!strchr(" \n", buf[i])) {
			break;
		}
	}
	buf[i + 1] = 0;

	return buf;
}

void
muon_readline_free(const char *line)
{
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
