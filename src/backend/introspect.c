/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "backend/introspect.h"
#include "backend/output.h"
#include "buf_size.h"
#include "error.h"
#include "lang/object_iterators.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/path.h"

static obj
introspect_custom_target(struct workspace *wk, struct project *proj, obj tgt)
{
	obj doc;
	doc = make_obj(wk, obj_dict);
	obj empty;
	empty = make_obj(wk, obj_array);

	struct obj_custom_target *t = get_obj_custom_target(wk, tgt);
	obj_dict_set(wk, doc, make_str(wk, "name"), t->name);
	obj_dict_set(wk,
		doc,
		make_str(wk, "id"),
		make_strf(wk, "%07x@@%s@cus", tgt, get_cstr(wk, t->name)));

	obj_dict_set(wk, doc, make_str(wk, "type"), make_str(wk, "custom"));
	obj_dict_set(wk, doc, make_str(wk, "defined_in"), obj_array_get_head(wk, obj_array_get_head(wk, t->callstack)));
	obj_dict_set(wk, doc, make_str(wk, "filename"), t->output);
	obj_dict_set(wk, doc, make_str(wk, "build_by_default"), make_obj_bool(wk, t->flags & custom_target_build_by_default));

	obj src;
	src = make_obj(wk, obj_array);
	obj src_unknown;
	src_unknown = make_obj(wk, obj_dict);
	obj_dict_set(wk, src_unknown, make_str(wk, "language"), make_str(wk, "unknown"));
	obj_dict_set(wk, src_unknown, make_str(wk, "compiler"), t->args);
	obj_dict_set(wk, src_unknown, make_str(wk, "parameters"), empty);
	obj_dict_set(wk, src_unknown, make_str(wk, "sources"), t->input ? t->input : empty);
	obj_array_push(wk, src, src_unknown);
	obj_dict_set(wk, doc, make_str(wk, "target_sources"), src);

	obj_dict_set(wk, doc, make_str(wk, "extra_files"), empty);
	obj_dict_set(wk, doc, make_str(wk, "subproject"), proj == arr_get(&wk->projects, 0) ? 0 : proj->cfg.name);

	return doc;
}

static obj
introspect_build_target(struct workspace *wk, struct project *proj, obj tgt)
{
	obj doc;
	doc = make_obj(wk, obj_dict);

	struct obj_build_target *t = get_obj_build_target(wk, tgt);
	obj_dict_set(wk, doc, make_str(wk, "name"), t->name);

	{
		const char *type = 0, *type_short = 0;
		if (t->type & tgt_executable) {
			type = "executable";
			type_short = "exe";
		} else if (t->type & tgt_static_library) {
			type = "static library";
			type_short = "sta";
		} else if (t->type & tgt_dynamic_library) {
			type = "shared library";
			type_short = "sha";
		} else if (t->type & tgt_shared_module) {
			type = "shared module";
			type_short = "sha";
		}

		obj_dict_set(wk,
			doc,
			make_str(wk, "id"),
			make_strf(wk, "%07x@@%s@%s", tgt, get_cstr(wk, t->name), type_short));

		obj_dict_set(wk, doc, make_str(wk, "type"), make_str(wk, type));
	}

	obj_dict_set(wk, doc, make_str(wk, "defined_in"), obj_array_get_head(wk, obj_array_get_head(wk, t->callstack)));

	obj filename;
	filename = make_obj(wk, obj_array);
	obj_array_push(wk, filename, t->build_path);
	obj_dict_set(wk, doc, make_str(wk, "filename"), filename);

	obj_dict_set(wk,
		doc,
		make_str(wk, "build_by_default"),
		make_obj_bool(wk, t->flags & build_tgt_flag_build_by_default));

	{
		obj src;
		src = make_obj(wk, obj_array);
		obj _lang, args, toolchain;
		obj_dict_for(wk, t->processed_args, _lang, args) {
			obj lang_src;
			lang_src = make_obj(wk, obj_dict);
			enum compiler_language lang = _lang;
			if (!obj_dict_geti(wk, proj->toolchains[t->machine], lang, &toolchain)) {
				UNREACHABLE;
			}

			struct obj_compiler *comp = get_obj_compiler(wk, toolchain);

			obj_dict_set(
				wk, lang_src, make_str(wk, "language"), make_str(wk, compiler_language_to_s(lang)));
			obj_dict_set(
				wk, lang_src, make_str(wk, "compiler"), comp->cmd_arr[toolchain_component_compiler]);
			obj_dict_set(wk, lang_src, make_str(wk, "parameters"), args);

			obj file_list, file;
			file_list = make_obj(wk, obj_array);
			obj_array_for(wk, t->src, file) {
				const char *path = get_file_path(wk, file);
				enum compiler_language file_lang;
				if (!filename_to_compiler_language(path, &file_lang)) {
					UNREACHABLE;
				}

				if (file_lang == lang) {
					obj_array_push(wk, file_list, *get_obj_file(wk, file));
				}
			}
			obj_dict_set(wk, lang_src, make_str(wk, "sources"), file_list);

			obj_dict_set(wk, lang_src, make_str(wk, "machine"), make_str(wk, machine_kind_to_s(t->machine)));

			obj_array_push(wk, src, lang_src);
		}

		{
			obj linker_src;
			linker_src = make_obj(wk, obj_dict);
			obj linker;
			if (!obj_dict_geti(wk, proj->toolchains[t->machine], t->dep_internal.link_language, &linker)) {
				UNREACHABLE;
			}
			struct obj_compiler *comp = get_obj_compiler(wk, linker);
			enum toolchain_component component = toolchain_component_linker;
			if (t->type & tgt_static_library) {
				component = toolchain_component_static_linker;
			} else if (toolchain_compiler_do_linker_passthrough(wk, comp)) {
				component = toolchain_component_compiler;
			}

			obj_dict_set(wk, linker_src, make_str(wk, "linker"), comp->cmd_arr[component]);
			obj_dict_set(wk, linker_src, make_str(wk, "parameters"), t->dep_internal.link_args);
			obj_array_push(wk, src, linker_src);
		}

		obj_dict_set(wk, doc, make_str(wk, "target_sources"), src);
	}

	obj_dict_set(wk, doc, make_str(wk, "extra_files"), t->extra_files);
	obj_dict_set(wk, doc, make_str(wk, "subproject"), proj == arr_get(&wk->projects, 0) ? 0 : proj->cfg.name);

	return doc;
}

static obj
introspect_targets(struct workspace *wk)
{
	obj doc;
	doc = make_obj(wk, obj_array);

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = arr_get(&wk->projects, i);
		obj tgt;
		obj_array_for(wk, proj->targets, tgt) {
			switch (get_obj_type(wk, tgt)) {
			case obj_alias_target: {
				// Not sure how to handle these?
				break;
			}
			case obj_both_libs: {
				struct obj_both_libs *libs = get_obj_both_libs(wk, tgt);
				obj_array_push(wk, doc, introspect_build_target(wk, proj, libs->static_lib));
				obj_array_push(wk, doc, introspect_build_target(wk, proj, libs->dynamic_lib));
				break;
			}
			case obj_build_target: {
				obj_array_push(wk, doc, introspect_build_target(wk, proj, tgt));
				break;
			}
			case obj_custom_target: {
				obj_array_push(wk, doc, introspect_custom_target(wk, proj, tgt));
				break;
			}
			default: UNREACHABLE;
			}
		}
	}

	return doc;
}

static obj
introspect_project(struct workspace *wk, struct project *proj)
{
	obj doc;
	doc = make_obj(wk, obj_dict);
	obj_dict_set(wk, doc, make_str(wk, "name"), proj->cfg.name);
	obj_dict_set(wk, doc, make_str(wk, "descriptive_name"), proj->cfg.name);
	obj_dict_set(wk, doc, make_str(wk, "version"), proj->cfg.version);
	obj_dict_set(wk, doc, make_str(wk, "subproject_dir"), proj->subprojects_dir);
	return doc;
}

static obj
introspect_projects(struct workspace *wk)
{
	struct project *proj = arr_get(&wk->projects, 0);
	obj doc = introspect_project(wk, proj);

	obj subs;
	subs = make_obj(wk, obj_array);
	uint32_t i;
	for (i = 1; i < wk->projects.len; ++i) {
		proj = arr_get(&wk->projects, i);
		obj_array_push(wk, subs, introspect_project(wk, proj));
	}

	obj_dict_set(wk, doc, make_str(wk, "subprojects"), subs);

	return doc;
}

static obj
introspect_option(struct workspace *wk, obj opt)
{
	obj doc;
	doc = make_obj(wk, obj_dict);
	struct obj_option *o = get_obj_option(wk, opt);

	obj_dict_set(wk, doc, make_str(wk, "name"), o->name);
	// TODO: This is wrong but muon doesn't currently have the concept of
	// option sections.
	obj_dict_set(wk, doc, make_str(wk, "section"), make_str(wk, "core"));
	obj_dict_set(wk, doc, make_str(wk, "description"), o->description);

	const char *type = build_option_type_to_s[o->type];
	if (o->type == op_feature) {
		type = "combo";
	}
	obj_dict_set(wk, doc, make_str(wk, "type"), make_str(wk, type));

	obj_dict_set(wk, doc, make_str(wk, "value"), o->val);

	obj choices = o->choices;
	if (o->type == op_feature) {
		choices = make_obj(wk, obj_array);
		obj_array_push(wk, choices, make_str(wk, "enabled"));
		obj_array_push(wk, choices, make_str(wk, "disabled"));
		obj_array_push(wk, choices, make_str(wk, "auto"));
	}

	if (choices) {
		obj_dict_set(wk, doc, make_str(wk, "choices"), choices);
	}

	return doc;
}

static obj
introspect_options(struct workspace *wk)
{
	obj doc;
	doc = make_obj(wk, obj_array);

	obj key, opt;
	obj_dict_for(wk, wk->global_opts, key, opt) {
		(void)key;
		obj_array_push(wk, doc, introspect_option(wk, opt));
	}

	struct project *proj = arr_get(&wk->projects, 0);
	obj_dict_for(wk, proj->opts, key, opt) {
		obj_array_push(wk, doc, introspect_option(wk, opt));
	}

	return doc;
}

static obj
introspect_buildsystem_files(struct workspace *wk)
{
	return wk->regenerate_deps;
}

static obj
introspect_dummy_array(struct workspace *wk)
{
	obj doc;
	doc = make_obj(wk, obj_array);
	return doc;
}

static obj
introspect_dummy_dict(struct workspace *wk)
{
	obj doc;
	doc = make_obj(wk, obj_dict);
	return doc;
}


typedef obj((*introspect_callback)(struct workspace *wk));

struct introspect_write_ctx {
	const char *name;
	introspect_callback cb;
};

static bool
introspect_write(struct workspace *wk, void *_ctx, FILE *f)
{
	struct introspect_write_ctx *ctx = _ctx;
	TSTR_FILE(buf, f);
	obj o = ctx->cb(wk);
	if (!o) {
		return false;
	}

	return obj_to_json(wk, o, &buf);
}

static bool
introspect_write_dummy(struct workspace *wk, void *_ctx, FILE *f)
{
	fprintf(f, "This file was generated by muon for meson compatibility.\n");
	return true;
}

bool
introspect_write_all(struct workspace *wk)
{
	TSTR(info_path);
	path_join(wk, &info_path, wk->build_root, output_path.introspect_dir);

	if (!fs_mkdir(info_path.buf, true)) {
		return false;
	}

	struct introspect_write_ctx files[] = {
		{ output_path.introspect_file.targets, introspect_targets },
		{ output_path.introspect_file.projectinfo, introspect_projects },
		{ output_path.introspect_file.buildoptions, introspect_options },
		{ output_path.introspect_file.buildsystem_files, introspect_buildsystem_files },

		{ output_path.introspect_file.benchmarks, introspect_dummy_array },
		{ output_path.introspect_file.compilers, introspect_dummy_array },
		{ output_path.introspect_file.dependencies, introspect_dummy_array },
		{ output_path.introspect_file.scan_dependencies, introspect_dummy_array },
		{ output_path.introspect_file.installed, introspect_dummy_dict },
		{ output_path.introspect_file.install_plan, introspect_dummy_array },
		{ output_path.introspect_file.machines, introspect_dummy_array },
		{ output_path.introspect_file.tests, introspect_dummy_array },
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(files); ++i) {
		if (!with_open(info_path.buf, files[i].name, wk, &files[i], introspect_write)) {
			return false;
		}
	}

	TSTR(meson_private_path);
	path_join(wk, &meson_private_path, wk->build_root, output_path.meson_private_dir);
	if (!fs_mkdir(meson_private_path.buf, true)) {
		return false;
	} else if (!with_open(meson_private_path.buf, "coredata.dat", wk, 0, introspect_write_dummy)) {
		return false;
	}

	return true;
}
