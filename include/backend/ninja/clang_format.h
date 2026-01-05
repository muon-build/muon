/*
 * SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_NINJA_CLANG_FORMAT_H
#define MUON_BACKEND_NINJA_CLANG_FORMAT_H

#include "lang/workspace.h"

bool ninja_clang_format_is_enabled_and_available(struct workspace *wk);
void ninja_clang_format_write_targets(struct workspace *wk, FILE *out);

#endif
