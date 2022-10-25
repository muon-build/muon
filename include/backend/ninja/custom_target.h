/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_NINJA_CUSTOM_TARGET_H
#define MUON_BACKEND_NINJA_CUSTOM_TARGET_H
#include "lang/workspace.h"
struct write_tgt_ctx;

bool ninja_write_custom_tgt(struct workspace *wk, obj tgt_id, struct write_tgt_ctx *ctx);
#endif
