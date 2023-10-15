/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_MODULES_PYTHON_H
#define MUON_FUNCTIONS_MODULES_PYTHON_H
#include "functions/common.h"

void python_build_impl_tbl(void);
extern struct func_impl_name impl_tbl_module_python[];
extern struct func_impl_name impl_tbl_module_python3[];

extern const struct func_impl_name impl_tbl_python_installation[];
#endif
