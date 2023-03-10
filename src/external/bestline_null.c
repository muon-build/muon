/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdio.h>

#include "external/bestline.h"
#include "log.h"

char *
muon_bestline(const char *prompt)
{
	static char buf[2048];
	fputs(prompt, log_file());
	fgets(buf, 2048, stdin);
	return buf;
}

void
muon_bestline_free(const char *line)
{
}

int
muon_bestline_history_add(const char *line)
{
	return 0;
}

void
muon_bestline_history_free(void)
{
}
