/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "backend/backend.h"
#include "backend/output.h"
#include "buf_size.h"
#include "embedded.h"
#include "error.h"
#include "lang/object_iterators.h"
#include "lang/serial.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "tracy.h"

struct project *
make_project(struct workspace *wk, uint32_t *id, const char *subproject_name, const char *cwd, const char *build_dir)
{
	*id = arr_push(&wk->projects, &(struct project){ 0 });
	struct project *proj = arr_get(&wk->projects, *id);

	proj->opts = make_obj(wk, obj_dict);
	proj->summary = make_obj(wk, obj_dict);
	proj->targets = make_obj(wk, obj_array);
	proj->tests = make_obj(wk, obj_array);
	proj->wrap_provides_deps = make_obj(wk, obj_dict);
	proj->wrap_provides_exes = make_obj(wk, obj_dict);

	for (uint32_t i = 0; i < machine_kind_count; ++i) {
		proj->toolchains[i] = make_obj(wk, obj_dict);
		proj->args[i] = make_obj(wk, obj_dict);
		proj->link_args[i] = make_obj(wk, obj_dict);
		proj->link_with[i] = make_obj(wk, obj_dict);
		proj->include_dirs[i] = make_obj(wk, obj_dict);
		proj->dep_cache.static_deps[i] = make_obj(wk, obj_dict);
		proj->dep_cache.shared_deps[i] = make_obj(wk, obj_dict);
	}

	proj->subprojects_dir = make_str(wk, "subprojects");

	if (subproject_name) {
		proj->subproject_name = make_str(wk, subproject_name);
	} else {
		proj->subproject_name = 0;
	}

	proj->cwd = make_str(wk, cwd);
	proj->source_root = proj->cwd;
	proj->build_dir = make_str(wk, build_dir);
	proj->build_root = proj->build_dir;

	proj->scope_stack = wk->vm.behavior.scope_stack_dup(wk, wk->vm.default_scope_stack);

	return proj;
}

struct project *
current_project(struct workspace *wk)
{
	return wk->projects.len ? arr_get(&wk->projects, wk->cur_project) : 0;
}

void
workspace_init_bare(struct workspace *wk)
{
	*wk = (struct workspace){ 0 };
	vm_init(wk);
	stack_init(&wk->stack, 4096);
	complex_types_init(wk, &wk->complex_types);

	{
#ifdef TRACY_ENABLE
		static bool first = true;
		if (first) {
			wk->tracy.is_master_workspace = true;
		}
		first = false;
#endif
	}
}

static bool
workspace_eval_startup_file(struct workspace *wk, const char *script)
{
	obj _;
	bool ret;
	struct source src;

	if (!embedded_get(script, &src)) {
		LOG_E("embedded script %s not found", script);
		return false;
	}

	stack_push(&wk->stack, wk->vm.lang_mode, language_extended);
	stack_push(&wk->stack, wk->vm.scope_stack, wk->vm.behavior.scope_stack_dup(wk, wk->vm.default_scope_stack));

	ret = eval(wk,
		&src,
		build_language_meson,
		0,
		&_);

	stack_pop(&wk->stack, wk->vm.scope_stack);
	stack_pop(&wk->stack, wk->vm.lang_mode);
	return ret;
}

void
workspace_init_runtime(struct workspace *wk)
{
	wk->argv0 = "dummy";
	wk->build_root = "dummy";

	TSTR(source_root);
	path_copy_cwd(wk, &source_root);
	wk->source_root = get_cstr(wk, tstr_into_str(wk, &source_root));

	arr_init(&wk->projects, 16, sizeof(struct project));
	arr_init(&wk->option_overrides, 32, sizeof(struct option_override));

	wk->binaries = make_obj(wk, obj_dict);
	wk->host_machine = make_obj(wk, obj_dict);
	wk->regenerate_deps = make_obj(wk, obj_array);
	wk->install = make_obj(wk, obj_array);
	wk->install_scripts = make_obj(wk, obj_array);
	wk->postconf_scripts = make_obj(wk, obj_array);
	wk->subprojects = make_obj(wk, obj_dict);
	wk->global_opts = make_obj(wk, obj_dict);
	wk->compiler_check_cache = make_obj(wk, obj_dict);
	wk->dependency_handlers = make_obj(wk, obj_dict);
	wk->finalizers = make_obj(wk, obj_array);

	for (uint32_t i = 0; i < machine_kind_count; ++i) {
		wk->toolchains[i] = make_obj(wk, obj_dict);
		wk->global_args[i] = make_obj(wk, obj_dict);
		wk->global_link_args[i] = make_obj(wk, obj_dict);
		wk->dep_overrides_static[i] = make_obj(wk, obj_dict);
		wk->dep_overrides_dynamic[i] = make_obj(wk, obj_dict);
		wk->find_program_overrides[i] = make_obj(wk, obj_dict);
	}
}

void
workspace_init_startup_files(struct workspace *wk)
{
	if (!init_global_options(wk)) {
		UNREACHABLE;
	}

	const char *startup_files[] = {
		"runtime/dependencies.meson",
	};

	for (uint32_t i = 0; i < ARRAY_LEN(startup_files); ++i) {
		if (!workspace_eval_startup_file(wk, startup_files[i])) {
			LOG_W("script %s failed to load", startup_files[i]);
		}
	}
}

void
workspace_init(struct workspace *wk)
{
	workspace_init_bare(wk);
	workspace_init_runtime(wk);
	workspace_init_startup_files(wk);
}

void
workspace_destroy_bare(struct workspace *wk)
{
	vm_destroy(wk);
	stack_destroy(&wk->stack);
}

void
workspace_destroy(struct workspace *wk)
{
	TracyCZoneAutoS;
	arr_destroy(&wk->projects);
	arr_destroy(&wk->option_overrides);
	workspace_destroy_bare(wk);
	TracyCZoneAutoE;
}

bool
workspace_setup_paths(struct workspace *wk, const char *build, const char *argv0, uint32_t argc, char *const argv[])
{
	TSTR(build_root);
	path_make_absolute(wk, &build_root, build);
	wk->build_root = get_cstr(wk, tstr_into_str(wk, &build_root));

	TSTR(argv0_resolved);
	if (fs_find_cmd(wk, &argv0_resolved, argv0)) {
		wk->argv0 = get_cstr(wk, tstr_into_str(wk, &argv0_resolved));
	} else {
		wk->argv0 = get_cstr(wk, make_str(wk, argv0));
	}

	wk->original_commandline.argc = argc;
	wk->original_commandline.argv = argv;

	TSTR(muon_private);
	path_join(wk, &muon_private, wk->build_root, output_path.private_dir);
	wk->muon_private = get_cstr(wk, tstr_into_str(wk, &muon_private));

	if (!fs_mkdir_p(wk->muon_private)) {
		return false;
	}

	TSTR(path);
	{
		const struct str *gitignore_src = &STR("*\n");
		path_join(wk, &path, wk->build_root, ".gitignore");
		if (!fs_write(path.buf, (const uint8_t *)gitignore_src->s, gitignore_src->len)) {
			return false;
		}
	}

	{
		const struct str *hgignore_src = &STR("syntax: glob\n**/*\n");
		path_join(wk, &path, wk->build_root, ".hgignore");
		if (!fs_write(path.buf, (const uint8_t *)hgignore_src->s, hgignore_src->len)) {
			return false;
		}
	}

	return true;
}

static enum iteration_result
print_summaries_line_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	FILE *out = _ctx;

	obj_fprintf(wk, out, "      %#o: %o\n", k, v);

	return ir_cont;
}

static enum iteration_result
print_summaries_section_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	FILE *out = _ctx;

	if (get_str(wk, k)->len) {
		obj_fprintf(wk, out, "    %#o\n", k);
	}

	obj_dict_foreach(wk, v, out, print_summaries_line_iter);
	return ir_cont;
}

void
workspace_print_summaries(struct workspace *wk, FILE *out)
{
	if (!out) {
		return;
	}

	bool printed_summary_header = false;
	uint32_t i;
	struct project *proj;
	for (i = 0; i < wk->projects.len; ++i) {
		proj = arr_get(&wk->projects, i);
		if (proj->not_ok) {
			continue;
		}

		struct obj_dict *d = get_obj_dict(wk, proj->summary);
		if (!d->len) {
			continue;
		}

		if (!printed_summary_header) {
			fprintf(out, "summary:\n");
			printed_summary_header = true;
		}

		fprintf(out, "- %s %s\n", get_cstr(wk, proj->cfg.name), get_cstr(wk, proj->cfg.version));
		obj_dict_foreach(wk, proj->summary, out, print_summaries_section_iter);
	}
}

static enum iteration_result
workspace_add_regenerate_deps_iter(struct workspace *wk, void *_ctx, obj v)
{
	TSTR(path);
	const char *s = get_cstr(wk, v);
	if (!path_is_absolute(s)) {
		path_join(wk, &path, workspace_cwd(wk), s);
		v = tstr_into_str(wk, &path);
		s = get_cstr(wk, v);
	}

	if (path_is_subpath(wk->build_root, s)) {
		return ir_cont;
	}

	if (!fs_file_exists(s)) {
		return ir_cont;
	}

	obj_array_push(wk, wk->regenerate_deps, v);
	return ir_cont;
}

void
workspace_add_regenerate_deps(struct workspace *wk, obj obj_or_arr)
{
	if (get_obj_type(wk, obj_or_arr) == obj_array) {
		obj_array_foreach(wk, obj_or_arr, NULL, workspace_add_regenerate_deps_iter);
	} else {
		workspace_add_regenerate_deps_iter(wk, NULL, obj_or_arr);
	}
}

const char *
workspace_cwd(struct workspace *wk)
{
	if (wk->vm.lang_mode == language_internal) {
		return path_cwd();
	} else {
		return get_cstr(wk, current_project(wk)->cwd);
	}
}

bool
workspace_do_setup(struct workspace *wk, const char *build, const char *argv0, uint32_t argc, char *const argv[])
{
	bool res = false;

	if (!workspace_setup_paths(wk, build, argv0, argc, argv)) {
		goto ret;
	}

	workspace_init_startup_files(wk);

	{
		TSTR(path);
		path_join(wk, &path, wk->muon_private, output_path.compiler_check_cache);
		if (fs_file_exists(path.buf)) {
			FILE *f;
			if ((f = fs_fopen(path.buf, "rb"))) {
				if (!serial_load(wk, &wk->compiler_check_cache, f)) {
					LOG_E("failed to load compiler check cache");
				}
				fs_fclose(f);
			}
		}
	}

	uint32_t project_id;
	if (!eval_project(wk, NULL, wk->source_root, wk->build_root, &project_id)) {
		goto ret;
	}

	log_plain("\n");

	obj finalizer;
	obj_array_for(wk, wk->finalizers, finalizer) {
		obj _;
		if (!vm_eval_capture(wk, finalizer, 0, 0, &_)) {
			goto ret;
		}
	}

	if (!backend_output(wk)) {
		goto ret;
	}

	workspace_print_summaries(wk, _log_file());

	LOG_I("setup complete");

	res = true;
ret:
	return res;
}
