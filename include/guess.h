/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_GUESS_H
#define MUON_GUESS_H
#include "lang/workspace.h"

bool guess_version(struct workspace *wk, const char *src, obj *res);
#endif
