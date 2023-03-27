/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdio.h>

void
print_colorized(FILE *out, const char *s)
{
	fwrite(s, 1, strlen(s), out);
}
