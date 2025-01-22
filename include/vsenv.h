/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_VSENV_H
#define MUON_VSENV_H
#include <stdbool.h>
bool vsenv_setup(const char *cache_path, bool force);
#endif
