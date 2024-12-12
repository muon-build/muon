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
#include "platform/path.h"

struct project *
make_project(struct workspace *wk, uint32_t *id, const char *subproject_name, const char *cwd, const char *build_dir)
{
	*id = arr_push(&wk->projects, &(struct project){ 0 });
	struct project *proj = arr_get(&wk->projects, *id);

	make_obj(wk, &proj->opts, obj_dict);
	make_obj(wk, &proj->summary, obj_dict);
	make_obj(wk, &proj->targets, obj_array);
	make_obj(wk, &proj->tests, obj_array);
	make_obj(wk, &proj->dep_cache.static_deps, obj_dict);
	make_obj(wk, &proj->dep_cache.shared_deps, obj_dict);
	make_obj(wk, &proj->wrap_provides_deps, obj_dict);
	make_obj(wk, &proj->wrap_provides_exes, obj_dict);

	for (uint32_t i = 0; i < machine_kind_count; ++i) {
		make_obj(wk, &proj->toolchains[i], obj_dict);
		make_obj(wk, &proj->args[i], obj_dict);
		make_obj(wk, &proj->link_args[i], obj_dict);
		make_obj(wk, &proj->link_with[i], obj_dict);
		make_obj(wk, &proj->include_dirs[i], obj_dict);
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
	machine_init();

	wk->argv0 = "dummy";
	wk->build_root = "dummy";

	SBUF(source_root);
	path_copy_cwd(wk, &source_root);
	wk->source_root = get_cstr(wk, sbuf_into_str(wk, &source_root));

	arr_init(&wk->projects, 16, sizeof(struct project));
	arr_init(&wk->option_overrides, 32, sizeof(struct option_override));

	make_obj(wk, &wk->binaries, obj_dict);
	make_obj(wk, &wk->host_machine, obj_dict);
	make_obj(wk, &wk->regenerate_deps, obj_array);
	make_obj(wk, &wk->install, obj_array);
	make_obj(wk, &wk->install_scripts, obj_array);
	make_obj(wk, &wk->postconf_scripts, obj_array);
	make_obj(wk, &wk->subprojects, obj_dict);
	make_obj(wk, &wk->global_opts, obj_dict);
	make_obj(wk, &wk->compiler_check_cache, obj_dict);
	make_obj(wk, &wk->dependency_handlers, obj_dict);
	make_obj(wk, &wk->finalizers, obj_array);

	for (uint32_t i = 0; i < machine_kind_count; ++i) {
		make_obj(wk, &wk->toolchains[i], obj_dict);
		make_obj(wk, &wk->global_args[i], obj_dict);
		make_obj(wk, &wk->global_link_args[i], obj_dict);
		make_obj(wk, &wk->dep_overrides_static[i], obj_dict);
		make_obj(wk, &wk->dep_overrides_dynamic[i], obj_dict);
		make_obj(wk, &wk->find_program_overrides[i], obj_dict);
	}
}

void
workspace_init_startup_files(struct workspace *wk)
{
	if (!init_global_options(wk)) {
		UNREACHABLE;
	}

	const char *startup_files[] = {
		"dependencies.meson",
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
	arr_destroy(&wk->projects);
	arr_destroy(&wk->option_overrides);
	workspace_destroy_bare(wk);
}

bool
workspace_setup_paths(struct workspace *wk, const char *build, const char *argv0, uint32_t argc, char *const argv[])
{
	SBUF(build_root);
	path_make_absolute(wk, &build_root, build);
	wk->build_root = get_cstr(wk, sbuf_into_str(wk, &build_root));

	if (path_is_basename(argv0)) {
		wk->argv0 = get_cstr(wk, make_str(wk, argv0));
	} else {
		SBUF(argv0_abs);
		path_make_absolute(wk, &argv0_abs, argv0);
		wk->argv0 = get_cstr(wk, sbuf_into_str(wk, &argv0_abs));
	}

	wk->original_commandline.argc = argc;
	wk->original_commandline.argv = argv;

	SBUF(muon_private);
	path_join(wk, &muon_private, wk->build_root, output_path.private_dir);
	wk->muon_private = get_cstr(wk, sbuf_into_str(wk, &muon_private));

	if (!fs_mkdir_p(wk->muon_private)) {
		return false;
	}

	SBUF(path);
	{
		const struct str *gitignore_src = &WKSTR("*\n");
		path_join(wk, &path, wk->build_root, ".gitignore");
		if (!fs_write(path.buf, (const uint8_t *)gitignore_src->s, gitignore_src->len)) {
			return false;
		}
	}

	{
		const struct str *hgignore_src = &WKSTR("syntax: glob\n**/*\n");
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
	SBUF(path);
	const char *s = get_cstr(wk, v);
	if (!path_is_absolute(s)) {
		path_join(wk, &path, workspace_cwd(wk), s);
		v = sbuf_into_str(wk, &path);
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
		SBUF(path);
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
