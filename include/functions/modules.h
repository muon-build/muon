/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_MODULES_H
#define MUON_FUNCTIONS_MODULES_H

#include "functions/common.h"

extern const char *module_names[module_count];
extern const struct func_impl impl_tbl_module[];
extern const struct func_impl *module_func_tbl[module_count][language_mode_count];

bool module_lookup(struct workspace *wk, const char *name, obj *res);
const struct func_impl *module_func_lookup(struct workspace *wk, const char *name, enum module mod);
#endif
