/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_UI_H
#define MUON_UI_H

#include <stdbool.h>

extern bool have_ui;
struct workspace;
bool ui_main(struct workspace *wk);

#endif
