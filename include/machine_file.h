/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_MACHINE_FILE_H
#define MUON_MACHINE_FILE_H

#include "lang/workspace.h"

bool machine_file_eval(struct workspace *wk, const char *path, enum machine_kind machine);
#endif
