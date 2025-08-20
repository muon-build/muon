/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_LOG_H
#define MUON_PLATFORM_LOG_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

void print_colorized(FILE *out, const char *s, uint32_t len, bool strip);
#endif
