/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef MUON_LANG_LSP_H
#define MUON_LANG_LSP_H
#include <stdbool.h>
struct az_opts;
struct workspace;
bool analyze_server(struct workspace *wk, struct az_opts *opts);
#endif
