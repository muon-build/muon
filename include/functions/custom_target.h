/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_CUSTOM_TARGET_H
#define MUON_FUNCTIONS_CUSTOM_TARGET_H
#include "functions/common.h"

bool custom_target_is_linkable(struct workspace *wk, obj ct);

extern const struct func_impl impl_tbl_custom_target[];
#endif
