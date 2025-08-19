/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_CUSTOM_TARGET_H
#define MUON_FUNCTIONS_CUSTOM_TARGET_H
#include "lang/func_lookup.h"

bool custom_target_is_linkable(struct workspace *wk, obj ct);

FUNC_REGISTER(custom_target);
#endif
