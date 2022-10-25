/*
 * SPDX-FileCopyrightText: dffdff2423 <dffdff2423@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_NINJA_ALIAS_TARGET_H
#define MUON_BACKEND_NINJA_ALIAS_TARGET_H

#include <stdio.h>

#include "lang/types.h"

struct workspace;
struct project;
struct write_tgt_ctx;

bool ninja_write_alias_tgt(struct workspace *wk, obj tgt_id, struct write_tgt_ctx *ctx);

#endif
