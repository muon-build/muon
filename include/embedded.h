/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_EMBEDDED_H
#define MUON_EMBEDDED_H
#include <stdbool.h>
struct source;
bool embedded_get(const char *name, struct source *src);
#endif
