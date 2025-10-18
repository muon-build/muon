/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_TERM_H
#define MUON_PLATFORM_TERM_H
#include <stdbool.h>
#include <stdint.h>

struct workspace;
bool term_winsize(struct workspace *wk, int fd, uint32_t *height, uint32_t *width);
#endif
