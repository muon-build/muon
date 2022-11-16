/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_FMT_H
#define MUON_LANG_FMT_H

#include <stdio.h>

#include "lang/parser.h"

bool fmt(struct source *src, FILE *out, const char *cfg_path, bool check_only);
#endif
