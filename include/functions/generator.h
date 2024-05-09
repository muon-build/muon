/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_GENERATOR_H
#define MUON_FUNCTIONS_GENERATOR_H
#include "lang/func_lookup.h"

bool
generated_list_process_for_target(struct workspace *wk, uint32_t err_node, obj gl, obj tgt, bool add_targets, obj *res);

extern const struct func_impl impl_tbl_generator[];
#endif
