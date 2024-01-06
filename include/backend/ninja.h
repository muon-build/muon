/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_NINJA_H
#define MUON_BACKEND_NINJA_H

#include "lang/workspace.h"

struct write_tgt_ctx {
	FILE *out;
	const struct project *proj;
	bool wrote_default;
};

bool ninja_write_all(struct workspace *wk);
bool ninja_run(struct workspace *wk, obj args, const char *chdir, const char *capture);
#endif
