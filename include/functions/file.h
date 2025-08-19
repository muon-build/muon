/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_FILE_H
#define MUON_FUNCTIONS_FILE_H
#include "lang/func_lookup.h"

bool file_is_dynamic_lib(struct workspace *wk, obj file);
bool file_is_static_lib(struct workspace *wk, obj file);
bool file_is_linkable(struct workspace *wk, obj file);

FUNC_REGISTER(file);
#endif
