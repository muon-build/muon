/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_NINJA_COVERAGE_H
#define MUON_BACKEND_NINJA_COVERAGE_H

#include "lang/workspace.h"

bool ninja_coverage_is_enabled_and_available(struct workspace *wk);
void ninja_coverage_write_targets(struct workspace *wk, FILE *out);

#endif
