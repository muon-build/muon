/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_CMD_SUBPROJECTS_H
#define MUON_CMD_SUBPROJECTS_H

#include <stdint.h>
#include <stdbool.h>

struct workspace;
bool cmd_subprojects(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[]);
#endif
