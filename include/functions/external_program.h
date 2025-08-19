/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_EXTERNAL_PROGRAM_H
#define MUON_FUNCTIONS_EXTERNAL_PROGRAM_H
#include "lang/func_lookup.h"

void find_program_guess_version(struct workspace *wk, obj cmd_array, obj version_argument, obj *ver);

FUNC_REGISTER(external_program);
#endif
