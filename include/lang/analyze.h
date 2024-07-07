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

enum az_branch_element_flag {
	az_branch_element_flag_pop = 1 << 0,
};
union az_branch_element {
	int64_t i64;
	struct az_branch_element_data {
		uint32_t ip;
		uint32_t flags;
	} data;
};

enum az_branch_type {
	az_branch_type_normal,
	az_branch_type_loop,
};

obj make_typeinfo(struct workspace *wk, type_tag t);
obj make_az_branch_element(struct workspace *wk, uint32_t ip, uint32_t flags);

bool az_diagnostic_name_to_enum(const char *name, enum az_diagnostic *ret);
void az_print_diagnostic_names(void);
void az_check_dead_code(struct workspace *wk, struct ast *ast);
void az_set_error(void);

extern struct func_impl_group az_func_impl_group;

bool do_analyze(struct az_opts *opts);
#endif
