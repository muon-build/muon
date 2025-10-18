/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_RPATH_FIXER_H
#define MUON_PLATFORM_RPATH_FIXER_H

#include <stdbool.h>
#include <stdio.h>

struct workspace;
bool fix_rpaths(struct workspace *wk, const char *elf_path, const char *build_root);
#endif
