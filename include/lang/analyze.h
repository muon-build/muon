/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_ANALYZE_H
#define MUON_LANG_ANALYZE_H

#include "error.h"
#include "lang/func_lookup.h"
#include "workspace.h"

struct ast;

enum az_diagnostic {
	az_diagnostic_unused_variable = 1 << 0,
	az_diagnostic_reassign_to_conflicting_type = 1 << 1,
	az_diagnostic_dead_code = 1 << 2,
};

struct az_opts {
	bool subdir_error;
	bool eval_trace;
	bool analyze_project_call_only;
	bool relaxed_parse;
	bool auto_chdir_root;
	enum error_diagnostic_store_replay_opts replay_opts;
	enum language_mode lang_mode;
	const char *single_file;
	uint64_t enabled_diagnostics;
	obj file_override;
	struct arr file_override_src;

	struct {
		bool debug_log, wait_for_debugger;
	} lsp;
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

struct az_assignment {
	const char *name;
	obj o;
	bool accessed, default_var;
	struct source_location location;
	uint32_t ip, src_idx;

	uint32_t ep_stacks_i;
	uint32_t ep_stack_len;
};

struct az_assignment *az_assign_lookup(struct workspace *wk, const char *name);
uint32_t az_dict_member_location_lookup_str(struct workspace *wk, obj dict, const char *key);

FUNC_REGISTER(analyzer);
extern struct func_impl_group az_func_impl_group;

void analyze_opts_init(struct workspace *wk, struct az_opts *opts);
void analyze_opts_destroy(struct workspace *wk, struct az_opts *opts);
bool analyze_opts_push_override(struct workspace *wk,
	struct az_opts *opts,
	const char *override,
	const char *content_path,
	const struct str *content);

bool do_analyze(struct workspace *wk, struct az_opts *opts);

void eval_trace_print(struct workspace *wk, obj trace);

bool analyze_project_call(struct workspace *wk, struct arena *a);
#endif
