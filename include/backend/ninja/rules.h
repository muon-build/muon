/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_NINJA_RULES_H
#define MUON_BACKEND_NINJA_RULES_H
#include "lang/workspace.h"

bool ninja_write_rules(FILE *out, struct workspace *wk, struct project *main_proj,
	bool need_phony, obj compiler_rule_arr);
#endif
