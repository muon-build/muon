/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_TERM_H
#define MUON_PLATFORM_TERM_H
#include <stdbool.h>
#include <stdint.h>

bool term_winsize(int fd, uint32_t *height, uint32_t *width);
#endif
