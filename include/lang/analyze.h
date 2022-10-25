/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_ANALYZE_H
#define MUON_LANG_ANALYZE_H

#include "error.h"
#include "workspace.h"

struct analyze_opts {
	bool subdir_error,
	     unused_variable_error;
	enum error_diagnostic_store_replay_opts replay_opts;
	const char *file_override;
};

bool do_analyze(struct analyze_opts *opts);
#endif
