/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <bestline.h>

#include "external/readline.h"
#include "platform/mem.h"

char *
muon_readline(const char *prompt)
{
	return bestlineWithHistory(prompt, 0);
}

void
muon_readline_free(const char *line)
{
	z_free((void *)line);
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
