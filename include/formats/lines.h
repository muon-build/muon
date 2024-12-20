/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FORMATS_LINES_H
#define MUON_FORMATS_LINES_H

#include <stddef.h>
#include <stdint.h>

#include "iterator.h"

typedef enum iteration_result((*each_line_callback)(void *ctx, char *line, size_t len));
typedef enum iteration_result((*each_line_const_callback)(void *ctx, const char *line, size_t len));

void each_line(char *buf, uint64_t len, void *ctx, each_line_callback cb);
void each_line_const(const char *buf, uint64_t len, void *ctx, each_line_const_callback cb);
#endif
