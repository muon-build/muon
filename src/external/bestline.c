/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include <bestline.h>

#include "external/bestline.h"
#include "platform/mem.h"

char *
muon_bestline(const char *prompt)
{
	return bestlineWithHistory(prompt, 0);
}

void
muon_bestline_free(const char *line)
{
	z_free((void *)line);
}

int
muon_bestline_history_add(const char *line)
{
	return bestlineHistoryAdd(line);
}

void
muon_bestline_history_free(void)
{
	bestlineHistoryFree();
}
