/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <bestline.h>
#include <string.h>

#include "external/readline.h"
#include "platform/mem.h"

char *
muon_readline(const char *prompt)
{
	static char buf[2048];
	char *line = bestlineWithHistory(prompt, 0);
	uint32_t line_len = strlen(line);
	line_len = line_len > sizeof(buf) - 1 ? sizeof(buf) - 1 : line_len;
	memcpy(buf, line, line_len);
	buf[line_len] = 0;
	z_free((void *)line);
	return buf;
}

int
muon_readline_history_add(const char *line)
{
	return bestlineHistoryAdd(line);
}

void
muon_readline_history_free(void)
{
	bestlineHistoryFree();
}
