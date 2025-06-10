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
#include "lang/workspace.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "tracy.h"
#include "version.h"

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
		proj->dep_cache.frameworks[i] = make_obj(wk, obj_dict);
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

	ret = eval(wk, &src, build_language_meson, 0, &_);

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
	wk->exclude_regenerate_deps = make_obj(wk, obj_array);
	wk->install = make_obj(wk, obj_array);
	wk->install_scripts = make_obj(wk, obj_array);
	wk->postconf_scripts = make_obj(wk, obj_array);
	wk->subprojects = make_obj(wk, obj_dict);
	wk->global_opts = make_obj(wk, obj_dict);
	wk->compiler_check_cache = make_obj(wk, obj_dict);
	wk->dependency_handlers = make_obj(wk, obj_dict);

	for (uint32_t i = 0; i < machine_kind_count; ++i) {
		wk->toolchains[i] = make_obj(wk, obj_dict);
		wk->global_args[i] = make_obj(wk, obj_dict);
		wk->global_link_args[i] = make_obj(wk, obj_dict);
		wk->dep_overrides_static[i] = make_obj(wk, obj_dict);
		wk->dep_overrides_dynamic[i] = make_obj(wk, obj_dict);
		wk->find_program_overrides[i] = make_obj(wk, obj_dict);
		wk->machine_properties[i] = make_obj(wk, obj_dict);
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

void
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
}

static bool
workspace_create_build_dir(struct workspace *wk)
{
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

static obj
summary_bool_to_s(struct workspace *wk, bool v, bool bool_yn)
{
	if (bool_yn) {
		return make_strf(wk, "%s", bool_to_yn(v));
	} else {
		return make_strf(wk, "%s%s" CLR(0), v ? CLR(c_green) : CLR(c_red), v ? "true" : "false");
	}
}

static obj
summary_item_to_s(struct workspace *wk, obj v, bool bool_yn)
{
	switch (get_obj_type(wk, v)) {
	case obj_dependency: {
		bool found = get_obj_dependency(wk, v)->flags & dep_flag_found;
		return summary_bool_to_s(wk, found, bool_yn);
	}
	case obj_external_program: {
		bool found = get_obj_external_program(wk, v)->found;
		return summary_bool_to_s(wk, found, bool_yn);
	}
	case obj_bool: {
		return summary_bool_to_s(wk, get_obj_bool(wk, v), bool_yn);
	}
	default: {
		TSTR(buf);
		obj_asprintf(wk, &buf, CLR(c_green) "%#o" CLR(0), v);
		return tstr_into_str(wk, &buf);
	}
	}
}

void
workspace_print_summaries(struct workspace *wk, FILE *out)
{
	if (!out) {
		return;
	}

	FILE *old_log_file = _log_file();
	log_set_file(out);

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

		LOG_I(CLR(c_magenta) "%s" CLR(0) " %s", get_cstr(wk, proj->cfg.name), get_cstr(wk, proj->cfg.version));

		obj section, values;
		obj_dict_for(wk, proj->summary, section, values) {
			if (get_str(wk, section)->len) {
				obj_lprintf(wk, log_info, "  %#o\n", section);
			}

			obj k, v;
			obj_dict_for(wk, values, k, v) {
				obj attr = obj_array_index(wk, v, 0);

				bool bool_yn = false;
				obj list_sep = 0;
				if (attr) {
					obj bool_yn_val;
					if (attr && obj_dict_index(wk, attr, make_str(wk, "bool_yn"), &bool_yn_val)) {
						bool_yn = get_obj_bool(wk, bool_yn_val);
					}

					obj_dict_index(wk, attr, make_str(wk, "list_sep"), &list_sep);
				}

				v = obj_array_index(wk, v, 1);

				obj_lprintf(wk, log_info, "    %#o: ", k);

				if (get_obj_type(wk, v) == obj_array) {
					obj sub_v;
					obj to_join = list_sep ? make_obj(wk, obj_array) : 0;

					if (!to_join) {
						log_plain(log_info, "\n");
					}

					obj_array_for(wk, v, sub_v) {
						sub_v = summary_item_to_s(wk, sub_v, bool_yn);
						if (to_join) {
							obj_array_push(wk, to_join, sub_v);
						} else {
							log_plain(log_info, "      ");

							const struct str *s = get_str(wk, sub_v);
							log_plain(log_info, "%s", s->s);
							log_plain(log_info, "\n");
						}
					}

					if (to_join) {
						obj joined;
						obj_array_join(wk, false, to_join, list_sep, &joined);
						const struct str *s = get_str(wk, joined);
						log_plain(log_info, "%s", s->s);
					}
				} else {
					const struct str *s = get_str(wk, summary_item_to_s(wk, v, bool_yn));
					log_plain(log_info, "%s", s->s);
				}

				log_plain(log_info, "\n");
			}
		}
	}

	log_set_file(old_log_file);
}

static obj
make_str_path_absolute(struct workspace *wk, obj v)
{
	TSTR(path);
	const char *s = get_cstr(wk, v);
	if (!path_is_absolute(s)) {
		path_join(wk, &path, workspace_cwd(wk), s);
		return tstr_into_str(wk, &path);
	}
	return v;
}

void
workspace_add_exclude_regenerate_dep(struct workspace *wk, obj v)
{
	v = make_str_path_absolute(wk, v);

	if (obj_array_in(wk, wk->exclude_regenerate_deps, v)) {
		return;
	}

	obj_array_push(wk, wk->exclude_regenerate_deps, v);
}

void
workspace_add_regenerate_dep(struct workspace *wk, obj v)
{
	if (!wk->regenerate_deps) {
		return;
	}

	v = make_str_path_absolute(wk, v);
	const char *s = get_cstr(wk, v);

	if (obj_array_in(wk, wk->exclude_regenerate_deps, v)) {
		return;
	} else if (path_is_subpath(wk->build_root, s)) {
		return;
	} else if (!fs_file_exists(s)) {
		return;
	}

	obj_array_push(wk, wk->regenerate_deps, v);
}

void
workspace_add_regenerate_deps(struct workspace *wk, obj obj_or_arr)
{
	if (get_obj_type(wk, obj_or_arr) == obj_array) {
		obj v;
		obj_array_for(wk, obj_or_arr, v) {
			workspace_add_regenerate_dep(wk, v);
		}
	} else {
		workspace_add_regenerate_dep(wk, obj_or_arr);
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

const char *
workspace_build_dir(struct workspace *wk)
{
	if (wk->vm.lang_mode == language_internal) {
		return wk->build_root;
	} else {
		return get_cstr(wk, current_project(wk)->build_dir);
	}
}


bool
workspace_do_setup(struct workspace *wk, const char *build, const char *argv0, uint32_t argc, char *const argv[])
{
	FILE *debug_file = 0;
	bool res = false;

	bool progress = log_is_progress_bar_enabled();
	log_progress_disable();

	workspace_setup_paths(wk, build, argv0, argc, argv);

	if (!workspace_create_build_dir(wk)) {
		goto ret;
	}

	{
		TSTR(path);
		path_join(wk, &path, wk->muon_private, output_path.debug_log);

		if (!(debug_file = fs_fopen(path.buf, "wb"))) {
			return false;
		}

		log_set_debug_file(debug_file);
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

	LOG_I("muon %s%s%s", muon_version.version, *muon_version.vcs_tag ? "-" : "", muon_version.vcs_tag);

	if (progress) {
		log_progress_enable();
		log_progress_set_style(&(struct log_progress_style) { .rate_limit = 64, .name_pad = 20 });
	}

	uint32_t project_id;
	if (!eval_project(wk, NULL, wk->source_root, wk->build_root, &project_id)) {
		goto ret;
	}

	if (log_is_progress_bar_enabled()) {
		log_progress_disable();
	} else {
		log_plain(log_info, "\n");
	}

	if (!backend_output(wk)) {
		goto ret;
	}

	workspace_print_summaries(wk, _log_file());

	LOG_I("setup complete");

	res = true;
ret:
	if (debug_file) {
		fs_fclose(debug_file);
		log_set_debug_file(0);
	}

	return res;
}
