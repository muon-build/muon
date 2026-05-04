/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_DAP_H
#define MUON_LANG_DAP_H
#include <stdbool.h>
struct workspace;
bool dap_init_pipe(struct workspace *wk, const char *pipe_path);
#endif
