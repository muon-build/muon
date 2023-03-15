/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_ANALYZE_H
#define MUON_LANG_ANALYZE_H

#include "error.h"
#include "workspace.h"

enum analyze_diagnostic {
	analyze_diagnostic_unused_variable = 1 << 0,
	analyze_diagnostic_reassign_to_conflicting_type = 1 << 1,
	analyze_diagnostic_dead_code = 1 << 2,
};

struct analyze_opts {
	bool subdir_error;
	enum error_diagnostic_store_replay_opts replay_opts;
	const char *file_override, *internal_file;
	uint64_t enabled_diagnostics;
};

bool analyze_diagnostic_name_to_enum(const char *name, enum analyze_diagnostic *ret);
void analyze_print_diagnostic_names(void);
void analyze_check_dead_code(struct workspace *wk, struct ast *ast);

bool do_analyze(struct analyze_opts *opts);
#endif
