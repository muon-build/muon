/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_BACKEND_H
#define MUON_BACKEND_BACKEND_H
#include "lang/workspace.h"

bool backend_output(struct workspace *wk);
void backend_print_stack(struct workspace *wk);
#endif
