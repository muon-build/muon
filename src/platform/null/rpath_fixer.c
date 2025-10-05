/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "platform/rpath_fixer.h"

bool
fix_rpaths(struct workspace *wk, const char *elf_path, const char *build_root)
{
	return true;
}
