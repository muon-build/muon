/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_WORKSPACE_H
#define MUON_LANG_WORKSPACE_H

#include "datastructures/arr.h"
#include "datastructures/stack.h"
#include "lang/eval.h"
#include "lang/string.h"
#include "lang/vm.h"
#include "compilers.h"

struct project {
	/* array of dicts */
	obj scope_stack;

	obj toolchains[machine_kind_count];
	obj args[machine_kind_count], link_args[machine_kind_count], include_dirs[machine_kind_count], link_with[machine_kind_count];

	obj source_root, build_root, cwd, build_dir, subproject_name;
	obj opts, targets, tests, test_setups, summary;
	struct {
		obj static_deps[machine_kind_count], shared_deps[machine_kind_count];
		obj frameworks[machine_kind_count];
	} dep_cache;
	obj wrap_provides_deps, wrap_provides_exes;

	// string
	obj rule_prefix;
	obj subprojects_dir;
	obj module_dir;

	struct {
		obj name;
		obj version;
		obj license;
		obj license_files;
		bool no_version;
	} cfg;

	bool not_ok; // set by failed subprojects
	bool initialized;

	// ninja-specific
	obj generic_rules[machine_kind_count];
};

enum workspace_init_flag {
	workspace_init_flag_arena = 1 << 0,
	workspace_init_flag_bare = 1 << 1,
	workspace_init_flag_runtime = 1 << 2,
	workspace_init_flag_startup_files = 1 << 3,
	workspace_init_flag_global_options = 1 << 4,
};

struct workspace {
	struct arena *a, *a_scratch;
	uint64_t a_pos, a_scratch_pos;

	const char *argv0, *source_root, *build_root, *muon_private;

	struct {
		uint32_t argc;
		char *const *argv;
	} original_commandline;

	/* Global objects
	 * ----------------- */

	obj toolchains[machine_kind_count];
	obj global_args[machine_kind_count], global_link_args[machine_kind_count];

	/* overridden dependencies dict */
	obj dep_overrides_static[machine_kind_count], dep_overrides_dynamic[machine_kind_count];
	/* overridden find_program dict */
	obj find_program_overrides[machine_kind_count];
	/* dict[str] */
	obj machine_properties[machine_kind_count];

	/* obj_array that tracks files for build regeneration */
	obj regenerate_deps;
	obj exclude_regenerate_deps;

	obj install;
	obj install_scripts;

	obj postconf_scripts;
	obj subprojects;
	/* global options */
	obj global_opts;
	/* dict[sha_512 -> [bool, any]] */
	obj compiler_check_cache;
	/* dict -> dict[method -> capture] */
	obj dependency_handlers;
	/* list[str], used for error reporting */
	obj backend_output_stack;
	/* ----------------- */

	struct vm vm;
	struct stack stack;

	struct arr projects;
	struct arr option_overrides;
	struct toolchain_registry toolchain_registry;

	uint32_t cur_project, init_flags;

	struct error_diagnostic_store *diagnostic_store;

#ifdef TRACY_ENABLE
	struct {
		bool is_master_workspace;
	} tracy;
#endif
};

void workspace_init_arena(struct workspace *wk, struct arena *a, struct arena *a_scratch);
void workspace_init_bare(struct workspace *wk, struct arena *a, struct arena *a_scratch);
void workspace_init_runtime(struct workspace *wk);
void workspace_init_startup_files(struct workspace *wk);
void workspace_setup_paths(struct workspace *wk, const char *build, const char *argv0, uint32_t argc, char *const argv[]);
void workspace_add_exclude_regenerate_dep(struct workspace *wk, obj v);
void workspace_add_regenerate_dep(struct workspace *wk, obj v);
void workspace_add_regenerate_deps(struct workspace *wk, obj obj_or_arr);

struct project *
make_project(struct workspace *wk, uint32_t *id, const char *subproject_name, const char *cwd, const char *build_dir);
void make_dummy_project(struct workspace *wk, bool setup_options);
struct project *current_project(struct workspace *wk);
const char *workspace_build_dir(struct workspace *wk);
const char *workspace_cwd(struct workspace *wk);

void workspace_print_summaries(struct workspace *wk, FILE *out);

enum workspace_do_setup_flag {
	workspace_do_setup_flag_clear_cache = 1 << 0,
};
bool workspace_do_setup_prepare(struct workspace *wk, const char *build, const char *argv0, uint32_t argc, char *const argv[], enum workspace_do_setup_flag flags);
bool workspace_do_setup(struct workspace *wk, struct arr *preload_files);
void workspace_scratch_begin(struct workspace *wk);
void workspace_scratch_end(struct workspace *wk);
void workspace_perm_begin(struct workspace *wk);
void workspace_perm_end(struct workspace *wk);
#endif
