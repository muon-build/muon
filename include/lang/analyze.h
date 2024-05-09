/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_ANALYZE_H
#define MUON_LANG_ANALYZE_H

#include "error.h"
#include "workspace.h"

enum az_diagnostic {
	az_diagnostic_unused_variable = 1 << 0,
	az_diagnostic_reassign_to_conflicting_type = 1 << 1,
	az_diagnostic_dead_code = 1 << 2,
	az_diagnostic_redirect_script_error = 1 << 3,
};

struct az_opts {
	bool subdir_error;
	bool eval_trace;
	enum error_diagnostic_store_replay_opts replay_opts;
	const char *file_override, *internal_file, *get_definition_for;
	uint64_t enabled_diagnostics;
};

bool az_diagnostic_name_to_enum(const char *name, enum az_diagnostic *ret);
void az_print_diagnostic_names(void);
void az_check_dead_code(struct workspace *wk, struct ast *ast);
void az_set_error(void);

extern struct func_impl_group az_func_impl_group;

bool do_analyze(struct az_opts *opts);
#endif
