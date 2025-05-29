/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-FileCopyrightText: illiliti <illiliti@thunix.net>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "functions/build_target.h"
#include "functions/file.h"
#include "functions/generator.h"
#include "functions/kernel/build_target.h"
#include "functions/kernel/dependency.h"
#include "install.h"
#include "lang/typecheck.h"
#include "log.h"
#include "machines.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"

enum build_target_kwargs {
	bt_kw_sources,
	bt_kw_include_directories,
	bt_kw_implicit_include_directories,
	bt_kw_dependencies,
	bt_kw_install,
	bt_kw_install_dir,
	bt_kw_install_mode,
	bt_kw_install_tag,
	bt_kw_link_with,
	bt_kw_link_whole,
	bt_kw_version,
	bt_kw_build_by_default,
	bt_kw_extra_files,
	bt_kw_target_type,
	bt_kw_name_prefix,
	bt_kw_name_suffix,
	bt_kw_soversion,
	bt_kw_link_depends,
	bt_kw_objects,
	bt_kw_pic,
	bt_kw_pie,
	bt_kw_build_rpath,
	bt_kw_install_rpath,
	bt_kw_export_dynamic,
	bt_kw_vs_module_defs, // TODO
	bt_kw_gnu_symbol_visibility,
	bt_kw_native,
	bt_kw_darwin_versions, // TODO
	bt_kw_implib, // TODO
	bt_kw_gui_app, // TODO
	bt_kw_link_language, // TODO
	bt_kw_win_subsystem, // TODO
	bt_kw_override_options,
	bt_kw_link_args,

#define E(lang, s) bt_kw_##lang##s
#define TOOLCHAIN_ENUM(lang) E(lang, _args), E(lang, _static_args), E(lang, _shared_args), E(lang, _pch),
	FOREACH_COMPILER_EXPOSED_LANGUAGE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
#undef E
		bt_kwargs_count,
};

static enum iteration_result
determine_linker_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_build_target *tgt = _ctx;

	enum compiler_language fl;

	if (!filename_to_compiler_language(get_file_path(wk, val), &fl)) {
		/* LOG_E("unable to determine language for '%s'", get_cstr(wk, src->dat.file)); */
		return ir_cont;
	}

	tgt->dep_internal.link_language = coalesce_link_languages(tgt->dep_internal.link_language, fl);

	return ir_cont;
}

static enum iteration_result
determine_linker_from_objects_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_build_target *tgt = _ctx;

	enum compiler_language fl;

	/*
	 * Try to see if the file looks like
	 * path/to/object.language.object_extension
	 *
	 * This means we expect two extensions, the first one will be stripped,
	 * and then the second will be used to determine the language of the
	 * file.
	 */

	const struct str *o = get_str(wk, *get_obj_file(wk, val));
	TSTR(path);
	path_basename(wk, &path, o->s);
	const char *first_dot = strrchr(path.buf, '.');
	path.len = first_dot - path.buf;
	path.buf[path.len] = 0;

	if (!strrchr(path.buf, '.')) {
		return ir_cont;
	}

	if (!filename_to_compiler_language(path.buf, &fl)) {
		/* LOG_E("unable to determine language for '%s'", get_cstr(wk, src->dat.file)); */
		return ir_cont;
	}

	tgt->dep_internal.link_language = coalesce_link_languages(tgt->dep_internal.link_language, fl);

	return ir_cont;
}

static bool
build_tgt_determine_linker(struct workspace *wk, uint32_t err_node, struct obj_build_target *tgt)
{
	if (!obj_array_foreach(wk, tgt->src, tgt, determine_linker_iter)) {
		return ir_err;
	}

	if (!obj_array_foreach(wk, tgt->objects, tgt, determine_linker_from_objects_iter)) {
		return ir_err;
	}

	if (!tgt->dep_internal.link_language) {
		enum compiler_language clink_langs[] = {
			compiler_language_c,
			compiler_language_cpp,
		};

		obj comp;
		uint32_t i;
		for (i = 0; i < ARRAY_LEN(clink_langs); ++i) {
			if (obj_dict_geti(wk, current_project(wk)->toolchains[tgt->machine], clink_langs[i], &comp)) {
				tgt->dep_internal.link_language = clink_langs[i];
				break;
			}
		}
	}

	if (!tgt->dep_internal.link_language) {
		vm_error_at(wk, err_node, "unable to determine linker for target");
		return false;
	}

	return true;
}

struct process_build_tgt_sources_ctx {
	uint32_t err_node;
	obj tgt_id;
	obj res;
	obj prepend_include_directories;
	bool implicit_include_directories;
};

static bool
process_source_include(struct workspace *wk, struct process_build_tgt_sources_ctx *ctx, obj val)
{
	const char *src = get_file_path(wk, val);

	if (!path_is_subpath(wk->build_root, src)) {
		return true;
	}

	TSTR(dir);
	TSTR(path);
	path_relative_to(wk, &path, wk->build_root, src);

	struct obj_build_target *tgt = get_obj_build_target(wk, ctx->tgt_id);
	obj_array_push(wk, tgt->dep_internal.order_deps, tstr_into_str(wk, &path));

	if (!tgt->dep_internal.raw.order_deps) {
		tgt->dep_internal.raw.order_deps = make_obj(wk, obj_array);
	}
	obj_array_push(wk, tgt->dep_internal.raw.order_deps, val);

	if (!ctx->implicit_include_directories) {
		return true;
	}

	path_dirname(wk, &dir, src);

	obj inc;
	inc = make_obj(wk, obj_include_directory);
	struct obj_include_directory *d = get_obj_include_directory(wk, inc);
	d->path = tstr_into_str(wk, &dir);
	obj_array_push(wk, ctx->prepend_include_directories, inc);

	return true;
}

static void
build_tgt_inc_required_compiler(struct workspace *wk, struct obj_build_target *tgt, enum compiler_language lang)
{
	obj n;
	if (obj_dict_geti(wk, tgt->required_compilers, lang, &n)) {
		obj_dict_seti(wk, tgt->required_compilers, lang, n + 1);
	} else {
		obj_dict_seti(wk, tgt->required_compilers, lang, 1);
	}
}


static enum iteration_result
build_tgt_push_source_files_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct process_build_tgt_sources_ctx *ctx = _ctx;
	struct obj_build_target *tgt = get_obj_build_target(wk, ctx->tgt_id);

	if (file_is_linkable(wk, val)) {
		obj_array_push(wk, tgt->dep_internal.link_with, val);
		return ir_cont;
	}

	enum compiler_language lang;
	if (!filename_to_compiler_language(get_file_path(wk, val), &lang) || languages[lang].is_header) {
		obj_array_push(wk, tgt->extra_files, val);

		// process every file that is either a header, or isn't
		// recognized, as a header
		if (!process_source_include(wk, ctx, val)) {
			return ir_err;
		}
		return ir_cont;
	} else if (languages[lang].is_linkable) {
		obj_array_push(wk, tgt->objects, val);
		return ir_cont;
	}

	build_tgt_inc_required_compiler(wk, tgt, lang);

	obj_array_push(wk, ctx->res, val);
	return ir_cont;
}

static enum iteration_result
process_build_tgt_sources_iter(struct workspace *wk, void *_ctx, obj val)
{
	obj res;
	struct process_build_tgt_sources_ctx *ctx = _ctx;

	switch (get_obj_type(wk, val)) {
	case obj_generated_list:
		if (!generated_list_process_for_target(wk, ctx->err_node, val, ctx->tgt_id, true, &res)) {
			return ir_err;
		}
		break;
	default: {
		if (!coerce_files(wk, ctx->err_node, val, &res)) {
			return ir_err;
		}
		break;
	}
	}

	obj_array_foreach(wk, res, ctx, build_tgt_push_source_files_iter);
	return ir_cont;
}

static bool
type_from_kw(struct workspace *wk, uint32_t node, obj t, enum tgt_type *res)
{
	const char *tgt_type = get_cstr(wk, t);
	struct {
		char *name;
		enum tgt_type type;
	} tgt_tbl[] = {
		{
			"executable",
			tgt_executable,
		},
		{
			"shared_library",
			tgt_dynamic_library,
		},
		{
			"shared_module",
			tgt_shared_module,
		},
		{
			"static_library",
			tgt_static_library,
		},
		{
			"both_libraries",
			tgt_dynamic_library | tgt_static_library,
		},
		{
			"library",
			get_option_default_library(wk),
		},
		{ 0 },
	};
	uint32_t i;

	for (i = 0; tgt_tbl[i].name; ++i) {
		if (strcmp(tgt_type, tgt_tbl[i].name) == 0) {
			*res = tgt_tbl[i].type;
			break;
		}
	}

	if (!tgt_tbl[i].name) {
		vm_error_at(wk, node, "unsupported target type '%s'", tgt_type);
		return false;
	}

	return true;
}

static void
setup_soname(struct workspace *wk, struct obj_build_target *tgt, const char *plain_name, obj sover, obj ver)
{
	char soversion[BUF_SIZE_1k] = { "." };
	bool have_soversion = false;

	if (sover) {
		have_soversion = true;
		strncpy(&soversion[1], get_cstr(wk, sover), BUF_SIZE_1k - 2);
	} else if (ver) {
		have_soversion = true;
		strncpy(&soversion[1], get_cstr(wk, ver), BUF_SIZE_1k - 2);
		char *p;
		if ((p = strchr(&soversion[1], '.'))) {
			*p = 0;
		}
	}

	tgt->soname = make_strf(wk, "%s%s", plain_name, have_soversion ? soversion : "");
}

static void
setup_dllname(struct workspace *wk, struct obj_build_target *tgt, const char *plain_name, obj dllver, obj ver)
{
	if (dllver) {
		tgt->soname = make_strf(wk, "%s-%s.dll", plain_name, get_cstr(wk, dllver));
	} else if (ver) {
		char buf[BUF_SIZE_1k];

		strncpy(buf, get_cstr(wk, ver), sizeof(buf) - 1);
		char *tmp = strchr(buf, '.');
		if (tmp) {
			*tmp = '\0';
			tgt->soname = make_strf(wk, "%s-%s.dll", plain_name, buf);
		}
	}

	if (!tgt->soname) {
		tgt->soname = make_strf(wk, "%s.dll", plain_name);
	}

	if (tgt->type == tgt_dynamic_library) {
		TSTR(implib);
		tstr_pushf(wk, &implib, "%s-implib.lib", plain_name);
		TSTR(path);
		path_join(wk, &path, get_cstr(wk, tgt->build_dir), implib.buf);
		tgt->implib = tstr_into_str(wk, &path);
	}
}

static bool
setup_shared_object_symlinks(struct workspace *wk,
	struct obj_build_target *tgt,
	const char *plain_name,
	obj *plain_name_install,
	obj *soname_install)
{
	TSTR(soname_symlink);
	TSTR(plain_name_symlink);

	if (!fs_mkdir_p(get_cstr(wk, tgt->build_dir))) {
		return false;
	}

	if (!str_eql(get_str(wk, tgt->build_name), get_str(wk, tgt->soname))) {
		path_join(wk, &soname_symlink, get_cstr(wk, tgt->build_dir), get_cstr(wk, tgt->soname));

		if (!fs_make_symlink(get_cstr(wk, tgt->build_name), soname_symlink.buf, true)) {
			return false;
		}

		*soname_install = tstr_into_str(wk, &soname_symlink);
	}

	if (!str_eql(&STRL(plain_name), get_str(wk, tgt->soname))
		&& !str_eql(&STRL(plain_name), get_str(wk, tgt->build_name))) {
		path_join(wk, &plain_name_symlink, get_cstr(wk, tgt->build_dir), plain_name);

		if (!fs_make_symlink(get_cstr(wk, tgt->soname), plain_name_symlink.buf, true)) {
			return false;
		}

		*plain_name_install = tstr_into_str(wk, &plain_name_symlink);
	}

	return true;
}

static bool
determine_target_build_name(struct workspace *wk,
	struct obj_build_target *tgt,
	obj sover,
	obj ver,
	obj name_pre,
	obj name_suff,
	char plain_name[BUF_SIZE_2k])
{
	char ver_dll[BUF_SIZE_1k];
	const char *pref, *suff, *ver_suff = NULL;

	*ver_dll = '\0';

	switch (tgt->type) {
	case tgt_executable:
		pref = "";
		if (host_machine.is_windows) {
			suff = "exe";
		} else {
			suff = NULL;
		}
		break;
	case tgt_static_library:
		if (host_machine.sys == machine_system_cygwin) {
			pref = "cyg";
		} else {
			pref = "lib";
		}
		suff = "a";
		break;
	case tgt_shared_module:
	case tgt_dynamic_library:
		if (host_machine.sys == machine_system_cygwin) {
			pref = "cyg";
		} else if (host_machine.sys == machine_system_windows) {
			pref = "";
		} else {
			pref = "lib";
		}

		if (host_machine.is_windows) {
			suff = "dll";
			if (sover) {
				strncpy(ver_dll, get_cstr(wk, sover), sizeof(ver_dll) - 1);
			} else if (ver) {
				strncpy(ver_dll, get_cstr(wk, ver), sizeof(ver_dll) - 1);
				char *ver_tmp = strchr(ver_dll, '.');
				if (ver_tmp) {
					*ver_tmp = 0;
				} else {
					*ver_dll = 0;
				}
			}
		} else if (host_machine.sys == machine_system_darwin) {
			suff = "dylib";
		} else {
			suff = "so";
			if (ver) {
				ver_suff = get_cstr(wk, ver);
			} else if (sover) {
				ver_suff = get_cstr(wk, sover);
			}
		}
		break;
	default: assert(false && "unreachable"); return false;
	}

	if (name_pre) {
		pref = get_cstr(wk, name_pre);
	}

	if (name_suff) {
		suff = get_cstr(wk, name_suff);
	}

	if (host_machine.is_windows) {
		snprintf(plain_name, BUF_SIZE_2k, "%s%s", pref, get_cstr(wk, tgt->name));
		tgt->build_name = make_strf(wk,
			"%s%s%s%s%s",
			plain_name,
			*ver_dll ? "-" : "",
			*ver_dll ? ver_dll : "",
			suff ? "." : "",
			suff ? suff : "");
	} else {
		snprintf(plain_name,
			BUF_SIZE_2k,
			"%s%s%s%s",
			pref,
			get_cstr(wk, tgt->name),
			suff ? "." : "",
			suff ? suff : "");
		tgt->build_name = make_strf(wk, "%s%s%s", plain_name, ver_suff ? "." : "", ver_suff ? ver_suff : "");
	}

	return true;
}

static bool
create_target(struct workspace *wk,
	struct args_norm *an,
	struct args_kw *akw,
	enum tgt_type type,
	bool ignore_sources,
	obj *res)
{
	char plain_name[BUF_SIZE_2k + 1] = { 0 };
	*res = make_obj(wk, obj_build_target);
	struct obj_build_target *tgt = get_obj_build_target(wk, *res);
	tgt->type = type;
	tgt->name = an[0].val;
	tgt->cwd = current_project(wk)->cwd;
	tgt->build_dir = current_project(wk)->build_dir;
	tgt->machine = coerce_machine_kind(wk, &akw[bt_kw_native]);
	tgt->callstack = vm_callstack(wk);
	tgt->args = make_obj(wk, obj_dict);
	tgt->src = make_obj(wk, obj_array);
	tgt->required_compilers = make_obj(wk, obj_dict);
	tgt->extra_files = make_obj(wk, obj_array);

	{ // dep internal setup
		enum build_dep_flag flags = 0;
		if (get_option_default_both_libraries(wk, 0, 0) == default_both_libraries_auto) {
			if (tgt->type & tgt_static_library) {
				flags |= build_dep_flag_both_libs_static;
				flags |= build_dep_flag_recursive;
			} else if (tgt->type & tgt_dynamic_library) {
				flags |= build_dep_flag_both_libs_shared;
				flags |= build_dep_flag_recursive;
			}
		}

		obj rpath = make_obj(wk, obj_array);
		if (akw[bt_kw_build_rpath].set) {
			obj_array_push(wk, rpath, akw[bt_kw_build_rpath].val);
		}

		if (akw[bt_kw_install_rpath].set) {
			obj_array_push(wk, rpath, akw[bt_kw_install_rpath].val);
		}

		struct build_dep_raw raw = {
			.link_with = akw[bt_kw_link_with].val,
			.link_whole = akw[bt_kw_link_whole].val,
			.deps = akw[bt_kw_dependencies].val,
			.rpath = rpath,
		};

		if (!dependency_create(wk, &raw, &tgt->dep_internal, flags)) {
			return false;
		}
	}

	if (!deps_check_machine_matches(wk,
		    tgt->name,
		    tgt->machine,
		    akw[bt_kw_link_with].val,
		    akw[bt_kw_link_whole].val,
		    akw[bt_kw_dependencies].val)) {
		return false;
	}

	if (akw[bt_kw_override_options].set) {
		if (!parse_and_set_override_options(wk,
			    akw[bt_kw_override_options].node,
			    akw[bt_kw_override_options].val,
			    &tgt->override_options)) {
			return false;
		}
	}

	{ // build target flags
		{ // pic
			bool pic = false;
			if (akw[bt_kw_pic].set) {
				pic = get_obj_bool(wk, akw[bt_kw_pic].val);

				if (!pic && tgt->type & (tgt_dynamic_library | tgt_shared_module)) {
					vm_error_at(
						wk, akw[bt_kw_pic].node, "shared libraries must be compiled as pic");
					return false;
				}
			} else {
				bool staticpic = get_option_bool(wk, tgt->override_options, "b_staticpic", true);

				if (tgt->type & tgt_static_library) {
					pic = staticpic;
				} else if (tgt->type & (tgt_dynamic_library | tgt_shared_module)) {
					pic = true;
				}
			}

			if (pic) {
				tgt->flags |= build_tgt_flag_pic;
			}
		}

		{ // pie
			bool pie = false;

			if (akw[bt_kw_pie].set) {
				pie = get_obj_bool(wk, akw[bt_kw_pie].val);

				if (pie && (tgt->type & tgt_executable) != tgt_executable) {
					vm_error_at(wk, akw[bt_kw_pie].node, "pie cannot be set for non-executables");
					return false;
				}
			} else if ((tgt->type & tgt_executable) == tgt_executable) {
				pie = get_option_bool(wk, tgt->override_options, "b_pie", false);
			}

			if (pie) {
				tgt->flags |= build_tgt_flag_pie;
			}
		}

		if (akw[bt_kw_export_dynamic].set && get_obj_bool(wk, akw[bt_kw_export_dynamic].val)) {
			tgt->flags |= build_tgt_flag_export_dynamic;
		}

		if (!akw[bt_kw_build_by_default].set || get_obj_bool(wk, akw[bt_kw_build_by_default].val)) {
			tgt->flags |= build_tgt_flag_build_by_default;
		}

		struct args_kw *vis = &akw[bt_kw_gnu_symbol_visibility];
		if (vis->set && get_str(wk, vis->val)->len) {
			const struct str *str = get_str(wk, vis->val);
			if (str_eql(str, &STR("default"))) {
				tgt->visibility = compiler_visibility_default;
			} else if (str_eql(str, &STR("hidden"))) {
				tgt->visibility = compiler_visibility_hidden;
			} else if (str_eql(str, &STR("internal"))) {
				tgt->visibility = compiler_visibility_internal;
			} else if (str_eql(str, &STR("protected"))) {
				tgt->visibility = compiler_visibility_protected;
			} else if (str_eql(str, &STR("inlineshidden"))) {
				tgt->visibility = compiler_visibility_inlineshidden;
			} else {
				vm_error_at(wk, vis->node, "unknown visibility '%s'", get_cstr(wk, vis->val));
				return false;
			}

			tgt->flags |= build_tgt_flag_visibility;
		}
	}

	obj sover = 0;
	if (akw[bt_kw_soversion].set) {
		if (!coerce_num_to_string(wk, akw[bt_kw_soversion].node, akw[bt_kw_soversion].val, &sover)) {
			return false;
		}
	}

	if (!determine_target_build_name(wk,
		    tgt,
		    sover,
		    akw[bt_kw_version].val,
		    akw[bt_kw_name_prefix].val,
		    akw[bt_kw_name_suffix].val,
		    plain_name)) {
		return false;
	}

	{ /* tgt_build_path */
		TSTR(path);
		path_join(wk, &path, get_cstr(wk, tgt->build_dir), get_cstr(wk, tgt->build_name));

		tgt->build_path = make_str(wk, path.buf);

		tstr_pushs(wk, &path, ".p");

		tgt->private_path = tstr_into_str(wk, &path);
	}

	bool implicit_include_directories = akw[bt_kw_implicit_include_directories].set ?
						    get_obj_bool(wk, akw[bt_kw_implicit_include_directories].val) :
						    true;
	obj prepend_include_directories = make_obj(wk, obj_array);

	{ // sources
		if (akw[bt_kw_objects].set) {
			if (!coerce_files(wk, akw[bt_kw_objects].node, akw[bt_kw_objects].val, &tgt->objects)) {
				return false;
			}

			obj deduped;
			obj_array_dedup(wk, tgt->objects, &deduped);
			tgt->objects = deduped;
		} else {
			tgt->objects = make_obj(wk, obj_array);
		}

		obj_array_extend(wk, tgt->objects, tgt->dep_internal.objects);
		obj_array_dedup_in_place(wk, &tgt->objects);

		if (!ignore_sources) {
			obj sources = an[1].val;

			if (akw[bt_kw_sources].set) {
				obj_array_extend(wk, sources, akw[bt_kw_sources].val);
			}

			obj_array_extend(wk, sources, tgt->dep_internal.sources);

			struct process_build_tgt_sources_ctx ctx = {
				.err_node = an[1].node,
				.res = tgt->src,
				.tgt_id = *res,
				.prepend_include_directories = prepend_include_directories,
				.implicit_include_directories = implicit_include_directories,
			};

			if (!obj_array_foreach_flat(wk, sources, &ctx, process_build_tgt_sources_iter)) {
				return false;
			}

			obj deduped;
			obj_array_dedup(wk, tgt->src, &deduped);
			tgt->src = deduped;
		}

		if (!get_obj_array(wk, tgt->src)->len && !get_obj_array(wk, tgt->objects)->len
			&& !akw[bt_kw_link_whole].set && tgt->type != tgt_static_library) {
			uint32_t node = akw[bt_kw_sources].set ? akw[bt_kw_sources].node : an[1].node;

			vm_error_at(wk, node, "target declared with no linkable sources");
			return false;
		}

		if (akw[bt_kw_extra_files].set) {
			obj_array_extend(wk, tgt->extra_files, akw[bt_kw_extra_files].val);
		}
	}

	{ // include directories
		uint32_t node = an[0].node; // TODO: not a very informative error node
		obj include_directories = make_obj(wk, obj_array);

		if (implicit_include_directories) {
			obj_array_push(wk, include_directories, current_project(wk)->cwd);
		}

		if (akw[bt_kw_include_directories].set) {
			node = akw[bt_kw_include_directories].node;

			obj_array_extend(wk, include_directories, akw[bt_kw_include_directories].val);
		}

		obj_array_extend_nodup(wk, include_directories, prepend_include_directories);

		obj coerced;
		if (!coerce_include_dirs(wk, node, include_directories, false, &coerced)) {
			return false;
		}

		obj_array_extend_nodup(wk, coerced, tgt->dep_internal.include_directories);
		tgt->dep_internal.include_directories = coerced;
	}

	{ // compiler args
		static struct {
			enum build_target_kwargs kw;
			enum compiler_language l;
			bool static_only, shared_only;
		} lang_args[] = {
#define E(lang, s, st, sh) { bt_kw_##lang##s, compiler_language_##lang, st, sh }
#define TOOLCHAIN_ENUM(lang) \
	E(lang, _args, false, false), E(lang, _static_args, true, false), E(lang, _shared_args, false, true),
			FOREACH_COMPILER_EXPOSED_LANGUAGE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
#undef E
		};

		{ // copy c or cpp args to assembly args
			if (akw[bt_kw_c_args].set) {
				obj_dict_seti(wk, tgt->args, compiler_language_assembly, akw[bt_kw_c_args].val);
			} else if (akw[bt_kw_cpp_args].set) {
				obj_dict_seti(wk, tgt->args, compiler_language_assembly, akw[bt_kw_cpp_args].val);
			}
		}

		uint32_t i;
		for (i = 0; i < ARRAY_LEN(lang_args); ++i) {
			if (!akw[lang_args[i].kw].set) {
				continue;
			} else if (lang_args[i].static_only && type != tgt_static_library) {
				continue;
			} else if (lang_args[i].shared_only && type != tgt_dynamic_library) {
				continue;
			}

			obj_dict_seti(wk, tgt->args, lang_args[i].l, akw[lang_args[i].kw].val);
		}
	}

	{ // pch
		struct {
			struct args_kw *kw;
			enum compiler_language l;
		} pch_args[] = {
#define E(lang, s) { &akw[bt_kw_##lang##s], compiler_language_##lang },
#define TOOLCHAIN_ENUM(lang) E(lang, _pch)
			FOREACH_COMPILER_EXPOSED_LANGUAGE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
#undef E
		};

		uint32_t i;
		for (i = 0; i < ARRAY_LEN(pch_args); ++i) {
			if (!pch_args[i].kw->set) {
				continue;
			}

			obj pch;

			if (get_obj_type(wk, pch_args[i].kw->val) == obj_build_target) {
				struct obj_build_target *pch_tgt = get_obj_build_target(wk, pch_args[i].kw->val);
				if ((pch_tgt->type & tgt_static_library) != tgt_static_library ) {
					vm_error_at(wk, pch_args[i].kw->node, "this build target must be a static library to be used as a pch");
					return false;
				}

				obj pch_tgt_pch;
				if (!obj_dict_geti(wk, pch_tgt->pch, pch_args[i].l, &pch_tgt_pch)) {
					vm_error_at(wk, pch_args[i].kw->node, "this build target does not define a pch for %s", compiler_language_to_s(pch_args[i].l));
					return false;
				}

				if (get_obj_type(wk, pch_tgt_pch) == obj_build_target) {
					vm_error_at(wk, pch_args[i].kw->node, "cannot use a build target for pch which itself uses a build target for pch");
					return false;
				}

				pch = pch_args[i].kw->val;
			} else {
				if (!coerce_file(wk, pch_args[i].kw->node, pch_args[i].kw->val, &pch)) {
					return false;
				}
			}

			if (!tgt->pch) {
				tgt->pch = make_obj(wk, obj_dict);
			}
			obj_dict_seti(wk, tgt->pch, pch_args[i].l, pch);

			build_tgt_inc_required_compiler(wk, tgt, pch_args[i].l);
		}
	}

	obj soname_install = 0, plain_name_install = 0;

	// soname handling
	if (type & (tgt_dynamic_library | tgt_shared_module)) {
		if (host_machine.is_windows) {
			setup_dllname(wk, tgt, plain_name, sover, akw[bt_kw_version].val);
		} else {
			setup_soname(wk, tgt, plain_name, sover, akw[bt_kw_version].val);

			if (type == tgt_dynamic_library) {
				if (!setup_shared_object_symlinks(
					    wk, tgt, plain_name, &plain_name_install, &soname_install)) {
					return false;
				}
			}
		}
	}

	// link depends
	if (akw[bt_kw_link_depends].set) {
		obj depends;
		if (!coerce_files(wk, akw[bt_kw_link_depends].node, akw[bt_kw_link_depends].val, &depends)) {
			return false;
		}

		tgt->link_depends = depends;
	}

	if (akw[bt_kw_install].set && get_obj_bool(wk, akw[bt_kw_install].val)) {
		tgt->flags |= build_tgt_flag_installed;

		obj install_dir = 0;
		if (akw[bt_kw_install_dir].set) {
			install_dir = akw[bt_kw_install_dir].val;
		} else {
			switch (type) {
			case tgt_executable: get_option_value(wk, current_project(wk), "bindir", &install_dir); break;
			case tgt_static_library:
				get_option_value(wk, current_project(wk), "libdir", &install_dir);
				break;
			case tgt_dynamic_library:
			case tgt_shared_module: {
				if (host_machine.is_windows) {
					get_option_value(wk, current_project(wk), "bindir", &install_dir);
				} else {
					get_option_value(wk, current_project(wk), "libdir", &install_dir);
				}
				break;
			}
			default: assert(false && "unreachable"); break;
			}
		}

		TSTR(install_src);
		path_join(wk, &install_src, get_cstr(wk, tgt->build_dir), get_cstr(wk, tgt->build_name));

		TSTR(install_dest);
		path_join(wk, &install_dest, get_cstr(wk, install_dir), get_cstr(wk, tgt->build_name));

		struct obj_install_target *install_tgt;
		if (!(install_tgt = push_install_target(wk,
			      tstr_into_str(wk, &install_src),
			      tstr_into_str(wk, &install_dest),
			      akw[bt_kw_install_mode].val))) {
			return false;
		}

		install_tgt->build_target = true;

		if (soname_install) {
			push_install_target_install_dir(wk, soname_install, install_dir, akw[bt_kw_install_mode].val);
		}

		if (plain_name_install) {
			push_install_target_install_dir(
				wk, plain_name_install, install_dir, akw[bt_kw_install_mode].val);
		}
	}

	if (!build_tgt_determine_linker(wk, an[0].node, tgt)) {
		return false;
	}

	tgt->dep = (struct build_dep){
		.link_language = tgt->dep_internal.link_language,
		.include_directories = tgt->dep_internal.include_directories,
		.order_deps = tgt->dep_internal.order_deps,
		.rpath = tgt->dep_internal.rpath,
		.raw = tgt->dep_internal.raw,
	};

	if (tgt->type == tgt_static_library) {
		tgt->dep.link_whole = tgt->dep_internal.link_whole;
		tgt->dep.link_with = tgt->dep_internal.link_with;
		tgt->dep.link_with_not_found = tgt->dep_internal.link_with_not_found;
		tgt->dep.frameworks = tgt->dep_internal.frameworks;
		obj_array_dup(wk, tgt->dep_internal.link_args, &tgt->dep.link_args);
	}

	if (akw[bt_kw_link_args].set) {
		obj_array_extend(wk, tgt->dep_internal.link_args, akw[bt_kw_link_args].val);
	}

	if (tgt->flags & build_tgt_generated_include) {
		const char *private_path = get_cstr(wk, tgt->private_path);

		// mkdir so that the include dir doesn't get pruned later on
		if (!fs_mkdir_p(private_path)) {
			return false;
		}
	}

	L("adding build target %s", get_cstr(wk, tgt->build_name));
	obj_array_push(wk, current_project(wk)->targets, *res);
	return true;
}

static bool
typecheck_string_or_empty_array(struct workspace *wk, struct args_kw *kw)
{
	if (!kw->set) {
		return true;
	}

	enum obj_type t = get_obj_type(wk, kw->val);
	if (t == obj_string) {
		return true;
	} else if (t == obj_array && get_obj_array(wk, kw->val)->len == 0) {
		kw->set = false;
		kw->val = 0;
		return true;
	} else {
		vm_error_at(wk, kw->node, "expected string or [], got %s", obj_type_to_s(t));
		return false;
	}
}

static bool
both_libs_can_reuse_objects(struct workspace *wk, struct args_kw *akw)
{
	bool lib_specific_args[] = {
#define E(lang, s) akw[bt_kw_##lang##s].set
#define TOOLCHAIN_ENUM(lang) E(lang, _static_args), E(lang, _shared_args),
		FOREACH_COMPILER_EXPOSED_LANGUAGE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
#undef E
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(lib_specific_args); ++i) {
		if (lib_specific_args[i]) {
			return false;
		}
	}

	return true;
}

static bool
tgt_common(struct workspace *wk, obj *res, enum tgt_type type, enum tgt_type argtype, bool tgt_type_from_kw)
{
	struct args_norm an[]
		= { { obj_string }, { TYPE_TAG_GLOB | tc_coercible_files | tc_generated_list }, ARG_TYPE_NULL };

	struct args_kw akw[bt_kwargs_count + 1] = {
		[bt_kw_sources] = { "sources", TYPE_TAG_LISTIFY | tc_coercible_files | tc_generated_list },
		[bt_kw_include_directories] = { "include_directories", TYPE_TAG_LISTIFY | tc_coercible_inc },
		[bt_kw_implicit_include_directories] = { "implicit_include_directories", obj_bool },
		[bt_kw_dependencies] = { "dependencies", TYPE_TAG_LISTIFY | tc_dependency },
		[bt_kw_install] = { "install", obj_bool },
		[bt_kw_install_dir] = { "install_dir", obj_string },
		[bt_kw_install_mode] = { "install_mode", tc_install_mode_kw },
		[bt_kw_install_tag] = { "install_tag", tc_string }, // TODO
		[bt_kw_link_with] = { "link_with", tc_link_with_kw },
		[bt_kw_link_whole] = { "link_whole", tc_link_with_kw },
		[bt_kw_version] = { "version", obj_string },
		[bt_kw_build_by_default] = { "build_by_default", obj_bool },
		[bt_kw_extra_files] = { "extra_files", TYPE_TAG_LISTIFY | tc_coercible_files },
		[bt_kw_target_type] = { "target_type", obj_string },
		[bt_kw_name_prefix] = { "name_prefix", tc_string | tc_array },
		[bt_kw_name_suffix] = { "name_suffix", tc_string | tc_array },
		[bt_kw_soversion] = { "soversion", tc_number | tc_string },
		[bt_kw_link_depends] = { "link_depends",
			TYPE_TAG_LISTIFY | tc_string | tc_file | tc_custom_target | tc_build_target | tc_build_target },
		[bt_kw_objects] = { "objects", TYPE_TAG_LISTIFY | tc_file | tc_string },
		[bt_kw_pic] = { "pic", obj_bool },
		[bt_kw_pie] = { "pie", obj_bool },
		[bt_kw_build_rpath] = { "build_rpath", obj_string },
		[bt_kw_install_rpath] = { "install_rpath", obj_string },
		[bt_kw_export_dynamic] = { "export_dynamic", obj_bool },
		[bt_kw_vs_module_defs] = { "vs_module_defs", tc_string | tc_file | tc_custom_target },
		[bt_kw_gnu_symbol_visibility] = { "gnu_symbol_visibility", obj_string },
		[bt_kw_native] = { "native", obj_bool },
		[bt_kw_darwin_versions] = { "darwin_versions", TYPE_TAG_LISTIFY | tc_string | tc_number },
		[bt_kw_implib] = { "implib", tc_bool | tc_string },
		[bt_kw_gui_app] = { "gui_app", obj_bool },
		[bt_kw_link_language] = { "link_language", obj_string },
		[bt_kw_win_subsystem] = { "win_subsystem", obj_string },
		[bt_kw_override_options] = { "override_options", COMPLEX_TYPE_PRESET(tc_cx_options_dict_or_list) },
		[bt_kw_link_args] = { "link_args", TYPE_TAG_LISTIFY | obj_string },
#define E(lang, s, t) [bt_kw_##lang##s] = { #lang #s, t }
#define TOOLCHAIN_ENUM(lang)                                                                                 \
	E(lang, _args, TYPE_TAG_LISTIFY | obj_string), E(lang, _static_args, TYPE_TAG_LISTIFY | obj_string), \
		E(lang, _shared_args, TYPE_TAG_LISTIFY | obj_string), E(lang, _pch, tc_string | tc_file | tc_build_target),
		FOREACH_COMPILER_EXPOSED_LANGUAGE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
#undef E
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (tgt_type_from_kw) {
		if (!akw[bt_kw_target_type].set) {
			vm_error(wk, "missing required kwarg: %s", akw[bt_kw_target_type].key);
			return false;
		}

		if (!type_from_kw(wk, akw[bt_kw_target_type].node, akw[bt_kw_target_type].val, &type)) {
			return false;
		}
	} else {
		if (akw[bt_kw_target_type].set) {
			vm_error_at(wk, akw[bt_kw_target_type].node, "invalid kwarg");
			return false;
		}
	}

	static const enum tgt_type keyword_validity[bt_kwargs_count] = {
		[bt_kw_version] = tgt_dynamic_library,
		[bt_kw_soversion] = tgt_dynamic_library,
	};

	uint32_t i;
	for (i = 0; i < bt_kwargs_count; ++i) {
		if (keyword_validity[i] && akw[i].set && !(keyword_validity[i] & argtype)) {
			vm_error_at(wk, akw[i].node, "invalid kwarg");
			return false;
		}
	}

	if (!typecheck_string_or_empty_array(wk, &akw[bt_kw_name_suffix])) {
		return false;
	} else if (!typecheck_string_or_empty_array(wk, &akw[bt_kw_name_prefix])) {
		return false;
	}

	if (type == (tgt_static_library | tgt_dynamic_library) && !akw[bt_kw_pic].set) {
		akw[bt_kw_pic].val = make_obj_bool(wk, true);
		akw[bt_kw_pic].set = true;
	}

	bool multi_target = false;
	obj tgt = 0;

	for (i = 0; i <= tgt_type_count; ++i) {
		enum tgt_type t = 1 << i;

		if (!(type & t)) {
			continue;
		}

		bool ignore_sources = false;

		if (tgt && !multi_target) {
			multi_target = true;
			*res = make_obj(wk, obj_array);
			obj_array_push(wk, *res, tgt);

			if (both_libs_can_reuse_objects(wk, akw)) {
				// If this target is a multi-target (both_libraries),
				// set the objects argument with objects from the
				// previous target

				obj objects;
				if (!build_target_extract_all_objects(wk, an[0].node, tgt, &objects, true)) {
					return false;
				}

				if (akw[bt_kw_objects].set) {
					obj_array_extend(wk, akw[bt_kw_objects].val, objects);
				} else {
					akw[bt_kw_objects].set = true;
					akw[bt_kw_objects].val = objects;
					akw[bt_kw_objects].node = an[0].node;
				}

				ignore_sources = true;
			}
		}

		if (!create_target(wk, an, akw, t, ignore_sources, &tgt)) {
			return false;
		}

		if (multi_target) {
			obj_array_push(wk, *res, tgt);
		} else {
			*res = tgt;
		}
	}

	if (multi_target) {
		obj val;
		val = make_obj(wk, obj_both_libs);
		struct obj_both_libs *both = get_obj_both_libs(wk, val);
		both->static_lib = obj_array_index(wk, *res, 0);
		both->dynamic_lib = obj_array_index(wk, *res, 1);
		*res = val;

		assert(get_obj_build_target(wk, both->static_lib)->type == tgt_static_library);
		assert(get_obj_build_target(wk, both->dynamic_lib)->type == tgt_dynamic_library);
	}

	return true;
}

bool
func_executable(struct workspace *wk, obj _, obj *res)
{
	return tgt_common(wk, res, tgt_executable, tgt_executable, false);
}

bool
func_static_library(struct workspace *wk, obj _, obj *res)
{
	return tgt_common(wk, res, tgt_static_library, tgt_static_library, false);
}

bool
func_shared_library(struct workspace *wk, obj _, obj *res)
{
	return tgt_common(wk, res, tgt_dynamic_library, tgt_dynamic_library, false);
}

bool
func_both_libraries(struct workspace *wk, obj _, obj *res)
{
	return tgt_common(
		wk, res, tgt_static_library | tgt_dynamic_library, tgt_static_library | tgt_dynamic_library, false);
}

bool
func_library(struct workspace *wk, obj _, obj *res)
{
	return tgt_common(wk, res, get_option_default_library(wk), tgt_static_library | tgt_dynamic_library, false);
}

bool
func_shared_module(struct workspace *wk, obj _, obj *res)
{
	return tgt_common(wk, res, tgt_shared_module, tgt_shared_module, false);
}

bool
func_build_target(struct workspace *wk, obj _, obj *res)
{
	return tgt_common(wk, res, 0, tgt_executable | tgt_static_library | tgt_dynamic_library, true);
}
