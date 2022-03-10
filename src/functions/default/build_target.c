#include "posix.h"

#include <assert.h>
#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "functions/build_target.h"
#include "functions/default/build_target.h"
#include "functions/default/options.h"
#include "functions/generator.h"
#include "lang/interpreter.h"
#include "log.h"
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
	bt_kw_link_with,
	bt_kw_link_whole,
	bt_kw_version,
	bt_kw_build_by_default,
	bt_kw_extra_files, // TODO
	bt_kw_target_type,
	bt_kw_name_prefix,
	bt_kw_name_suffix,
	bt_kw_soversion,
	bt_kw_link_depends,
	bt_kw_objects,
	bt_kw_pic,
	bt_kw_install_rpath, // TODO
	bt_kw_export_dynamic,
	bt_kw_vs_module_defs, // TODO
	bt_kw_gnu_symbol_visibility, // TODO
	bt_kw_native, // TODO

	/* lang args */
	bt_kw_c_pch, // TODO
	bt_kw_c_args,
	bt_kw_cpp_args,
	bt_kw_objc_args,
	bt_kw_link_args,

	bt_kwargs_count,
};

struct add_dep_sources_ctx {
	uint32_t node;
	obj src;
};

static enum iteration_result
add_dep_sources_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct add_dep_sources_ctx *ctx = _ctx;

	enum obj_type t = get_obj_type(wk, val);

	switch (t) {
	case obj_external_library:
		return ir_cont;
	case obj_dependency:
		break;
	default:
		interp_error(wk, ctx->node, "invalid dependency: %o", val);
		return ir_err;
	}

	struct obj_dependency *dep = get_obj_dependency(wk, val);

	if (!(dep->flags & dep_flag_no_sources)) {
		if (dep->sources) {
			obj src;
			obj_array_dup(wk, dep->sources, &src);
			obj_array_extend(wk, ctx->src, src);
		}

		if (dep->deps) {
			obj_array_foreach(wk, dep->deps, ctx, add_dep_sources_iter);
		}
	}

	return ir_cont;
}

struct process_build_tgt_sources_ctx {
	uint32_t err_node;
	obj tgt_id;
	obj res;
};

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

	obj_array_extend(wk, ctx->res, res);
	return ir_cont;
}

static enum tgt_type
default_library_type(struct workspace *wk)
{
	obj opt;
	get_option(wk, current_project(wk), "default_library", &opt);

	if (str_eql(get_str(wk, opt), &WKSTR("static"))) {
		return tgt_static_library;
	} else if (str_eql(get_str(wk, opt), &WKSTR("shared"))) {
		return tgt_dynamic_library;
	} else {
		assert(false && "unreachable");
		return 0;
	}
}

static bool
type_from_kw(struct workspace *wk, uint32_t node, obj t, enum tgt_type *res)
{
	const char *tgt_type = get_cstr(wk, t);
	struct { char *name; enum tgt_type type; } tgt_tbl[] = {
		{ "executable", tgt_executable, },
		{ "shared_library", tgt_dynamic_library, },
		{ "static_library", tgt_static_library, },
		{ "both_libraries", tgt_dynamic_library | tgt_static_library, },
		{ "library", default_library_type(wk), },
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
		interp_error(wk, node, "unsupported target type '%s'", tgt_type);
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

static bool
setup_shared_object_symlinks(struct workspace *wk, struct obj_build_target *tgt,
	const char *plain_name, const char **plain_name_install,
	const char **soname_install)
{
	static char soname_symlink[PATH_MAX], plain_name_symlink[PATH_MAX];

	if (!fs_mkdir_p(get_cstr(wk, tgt->build_dir))) {
		return false;
	}

	if (!str_eql(get_str(wk, tgt->build_name), get_str(wk, tgt->soname))) {
		if (!path_join(soname_symlink, PATH_MAX, get_cstr(wk, tgt->build_dir),
			get_cstr(wk, tgt->soname))) {
			return false;
		}

		if (!fs_make_symlink(get_cstr(wk, tgt->build_name), soname_symlink, true)) {
			return false;
		}

		*soname_install = soname_symlink;
	}

	if (!str_eql(get_str(wk, tgt->soname), &WKSTR(plain_name))) {
		if (!path_join(plain_name_symlink, PATH_MAX, get_cstr(wk, tgt->build_dir), plain_name)) {
			return false;
		}

		if (!fs_make_symlink(get_cstr(wk, tgt->soname), plain_name_symlink, true)) {
			return false;
		}

		*plain_name_install = plain_name_symlink;
	}

	return true;
}

static bool
determine_target_build_name(struct workspace *wk, struct obj_build_target *tgt, obj sover, obj ver,
	obj name_pre, obj name_suff, char plain_name[BUF_SIZE_2k])
{
	const char *pref, *suff, *ver_suff = NULL;

	switch (tgt->type) {
	case tgt_executable:
		pref = "";
		suff = "";
		break;
	case tgt_static_library:
		pref = "lib";
		suff = ".a";
		break;
	case tgt_shared_module:
	case tgt_dynamic_library:
		pref = "lib";
		suff = ".so";
		if (ver) {
			ver_suff = get_cstr(wk, ver);
		} else if (sover) {
			ver_suff = get_cstr(wk, sover);
		}
		break;
	default:
		assert(false && "unreachable");
		return false;
	}

	if (name_pre) {
		pref = get_cstr(wk, name_pre);
	}

	if (name_suff) {
		suff = get_cstr(wk, name_suff);
	}

	snprintf(plain_name, BUF_SIZE_2k, "%s%s%s", pref, get_cstr(wk, tgt->name), suff);

	tgt->build_name = make_strf(wk, "%s%s%s", plain_name, ver_suff ? "." : "", ver_suff ? ver_suff : "");
	return true;
}

static bool
create_target(struct workspace *wk, struct args_norm *an, struct args_kw *akw, enum tgt_type type, obj *res)
{
	char plain_name[BUF_SIZE_2k];
	make_obj(wk, res, obj_build_target);
	struct obj_build_target *tgt = get_obj_build_target(wk, *res);
	tgt->type = type;
	tgt->name = an[0].val;
	tgt->cwd = current_project(wk)->cwd;
	tgt->build_dir = current_project(wk)->build_dir;
	make_obj(wk, &tgt->args, obj_dict);

	{ // build target flags
		if (akw[bt_kw_link_whole].set) {
			tgt->flags |= build_tgt_flag_link_whole;
		}

		if (akw[bt_kw_pic].set) {
			tgt->flags |= build_tgt_flag_pic;
		}

		if (akw[bt_kw_export_dynamic].set) {
			tgt->flags |= build_tgt_flag_export_dynamic;
		}

		if (!akw[bt_kw_build_by_default].set || get_obj_bool(wk, akw[bt_kw_build_by_default].val)) {
			tgt->flags |= build_tgt_flag_build_by_default;
		}
	}

	if (!determine_target_build_name(wk, tgt, akw[bt_kw_soversion].val, akw[bt_kw_version].val,
		akw[bt_kw_name_prefix].val, akw[bt_kw_name_suffix].val, plain_name)) {
		return false;
	}

	{ /* tgt_build_path */
		char path[PATH_MAX] = { 0 };
		if (!path_join(path, PATH_MAX, get_cstr(wk, tgt->build_dir), get_cstr(wk, tgt->build_name))) {
			return false;
		}

		tgt->build_path = make_str(wk, path);

		if (!path_add_suffix(path, PATH_MAX, ".p")) {
			return false;
		}

		tgt->private_path = make_str(wk, path);
	}

	{ // sources
		if (akw[bt_kw_sources].set) {
			obj arr;
			obj_array_dup(wk, akw[bt_kw_sources].val, &arr);
			obj_array_extend(wk, an[1].val, arr);
		}

		if (akw[bt_kw_objects].set) {
			obj arr;
			obj_array_dup(wk, akw[bt_kw_objects].val, &arr);
			obj_array_extend(wk, an[1].val, arr);
		}

		make_obj(wk, &tgt->src, obj_array);

		struct process_build_tgt_sources_ctx ctx = {
			.err_node = an[1].node,
			.res = tgt->src,
			.tgt_id = *res,
		};

		if (!obj_array_foreach_flat(wk, an[1].val, &ctx, process_build_tgt_sources_iter)) {
			return false;
		}

		if (!get_obj_array(wk, tgt->src)->len && !akw[bt_kw_link_whole].set) {
			uint32_t node = akw[bt_kw_sources].set? akw[bt_kw_sources].node : an[1].node;

			interp_error(wk, node, "sources must not be empty unless link_whole is specified");
			return false;
		}
	}

	{ // include directories
		obj inc_dirs;
		make_obj(wk, &inc_dirs, obj_array);
		uint32_t node = an[0].node; // TODO: not a very informative error node

		if (!(akw[bt_kw_implicit_include_directories].set
		      && !get_obj_bool(wk, akw[bt_kw_implicit_include_directories].val))) {
			obj_array_push(wk, inc_dirs, current_project(wk)->cwd);
		}

		if (akw[bt_kw_include_directories].set) {
			node = akw[bt_kw_include_directories].node;

			obj inc;
			obj_array_dup(wk, akw[bt_kw_include_directories].val, &inc);
			obj_array_extend(wk, inc_dirs, inc);
		}

		obj coerced;
		if (!coerce_include_dirs(wk, node, inc_dirs, false, &coerced)) {
			return false;
		}

		tgt->include_directories = coerced;
	}

	{ // dependencies
		if (akw[bt_kw_dependencies].set) {
			tgt->deps = akw[bt_kw_dependencies].val;
			struct add_dep_sources_ctx ctx = {
				.node = akw[bt_kw_dependencies].node,
				.src = tgt->src,
			};

			obj_array_foreach(wk, tgt->deps, &ctx, add_dep_sources_iter);
		}
	}

	{ // compiler args
		static struct {
			enum build_target_kwargs kw;
			enum compiler_language l;
		} lang_args[] = {
			{ bt_kw_c_args, compiler_language_c },
			{ bt_kw_cpp_args, compiler_language_cpp },
			/* { bt_kw_objc_args, compiler_language_objc }, */
		};

		uint32_t i;
		for (i = 0; i < ARRAY_LEN(lang_args); ++i) {
			if (akw[lang_args[i].kw].set) {
				obj_dict_seti(wk, tgt->args, lang_args[i].l, akw[lang_args[i].kw].val);
			}
		}
	}

	{ // linker args
		if (akw[bt_kw_link_args].set) {
			tgt->link_args = akw[bt_kw_link_args].val;
		}

		make_obj(wk, &tgt->link_with, obj_array);
		if (akw[bt_kw_link_with].set) {
			obj arr;
			obj_array_dup(wk, akw[bt_kw_link_with].val, &arr);
			obj_array_extend(wk, tgt->link_with, arr);
		}

		if (akw[bt_kw_link_whole].set) {
			obj arr;
			obj_array_dup(wk, akw[bt_kw_link_whole].val, &arr);
			obj_array_extend(wk, tgt->link_with, arr);
		}
	}

	const char *soname_install = NULL, *plain_name_install = NULL;

	// soname handling
	if (type & (tgt_dynamic_library | tgt_shared_module)) {
		setup_soname(wk, tgt, plain_name, akw[bt_kw_soversion].val, akw[bt_kw_version].val);

		if (type == tgt_dynamic_library) {
			if (!setup_shared_object_symlinks(wk, tgt, plain_name,
				&plain_name_install, &soname_install)) {
				return false;
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
		obj install_dir = 0;
		if (akw[bt_kw_install_dir].set) {
			install_dir = akw[bt_kw_install_dir].val;
		} else {
			switch (type) {
			case tgt_executable:
				get_option(wk, current_project(wk), "bindir", &install_dir);
				break;
			case tgt_dynamic_library:
			case tgt_static_library:
			case tgt_shared_module:
				get_option(wk, current_project(wk), "libdir", &install_dir);
				break;
			default:
				assert(false && "unreachable");
				break;
			}
		}

		struct obj_install_target *install_tgt;
		if (!(install_tgt = push_install_target_basename(wk, tgt->build_dir,
			tgt->build_name, install_dir, akw[bt_kw_install_mode].val))) {
			return false;
		}

		install_tgt->build_target = true;

		if (soname_install) {
			push_install_target_install_dir(wk, make_str(wk, soname_install),
				install_dir, akw[bt_kw_install_mode].val);
		}

		if (plain_name_install) {
			push_install_target_install_dir(wk, make_str(wk, plain_name_install),
				install_dir, akw[bt_kw_install_mode].val);
		}
	}

	LOG_I("added target %s", get_cstr(wk, tgt->build_name));
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
		interp_error(wk, kw->node, "expected string or [], got %s", obj_type_to_s(t));
		return false;
	}
}

static bool
tgt_common(struct workspace *wk, uint32_t args_node, obj *res, enum tgt_type type, enum tgt_type argtype, bool tgt_type_from_kw)
{
	struct args_norm an[] = { { obj_string }, { ARG_TYPE_GLOB }, ARG_TYPE_NULL };

	struct args_kw akw[] = {
		[bt_kw_sources] = { "sources", ARG_TYPE_ARRAY_OF | obj_any },
		[bt_kw_include_directories] = { "include_directories", ARG_TYPE_ARRAY_OF | obj_any },
		[bt_kw_implicit_include_directories] = { "implicit_include_directories", obj_bool },
		[bt_kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_any },
		[bt_kw_install] = { "install", obj_bool },
		[bt_kw_install_dir] = { "install_dir", obj_string },
		[bt_kw_install_mode] = { "install_mode", ARG_TYPE_ARRAY_OF | obj_any },
		[bt_kw_link_with] = { "link_with", ARG_TYPE_ARRAY_OF | obj_any },
		[bt_kw_link_whole] = { "link_whole", ARG_TYPE_ARRAY_OF | obj_any },
		[bt_kw_version] = { "version", obj_string },
		[bt_kw_build_by_default] = { "build_by_default", obj_bool },
		[bt_kw_extra_files] = { "extra_files", obj_any }, // ignored
		[bt_kw_target_type] = { "target_type", obj_string },
		[bt_kw_name_prefix] = { "name_prefix", obj_any },
		[bt_kw_name_suffix] = { "name_suffix", obj_any },
		[bt_kw_soversion] = { "soversion", obj_any },
		[bt_kw_link_depends] = { "link_depends", ARG_TYPE_ARRAY_OF | obj_any },
		[bt_kw_objects] = { "objects", ARG_TYPE_ARRAY_OF | obj_file },
		[bt_kw_pic] = { "pic", obj_bool },
		[bt_kw_install_rpath] = { "install_rpath", obj_string },
		[bt_kw_export_dynamic] = { "export_dynamic", obj_bool },
		[bt_kw_vs_module_defs] = { "vs_module_defs", obj_any },
		[bt_kw_gnu_symbol_visibility] = { "gnu_symbol_visibility", obj_string },
		[bt_kw_native] = { "native", obj_bool },
		/* lang args */
		[bt_kw_c_pch] = { "c_pch", obj_any, },
		[bt_kw_c_args] = { "c_args", ARG_TYPE_ARRAY_OF | obj_string },
		[bt_kw_cpp_args] = { "cpp_args", ARG_TYPE_ARRAY_OF | obj_string },
		[bt_kw_objc_args] = { "objc_args", ARG_TYPE_ARRAY_OF | obj_string },
		[bt_kw_link_args] = { "link_args", ARG_TYPE_ARRAY_OF | obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (tgt_type_from_kw) {
		if (!akw[bt_kw_target_type].set) {
			interp_error(wk, args_node, "missing required kwarg: %s", akw[bt_kw_target_type].key);
			return false;
		}

		if (!type_from_kw(wk, akw[bt_kw_target_type].node, akw[bt_kw_target_type].val, &type)) {
			return false;
		}
	} else {
		if (akw[bt_kw_target_type].set) {
			interp_error(wk, akw[bt_kw_target_type].node, "invalid kwarg");
			return false;
		}
	}

	static const enum tgt_type keyword_validity[bt_kwargs_count] = {
		[bt_kw_version] = tgt_dynamic_library,
		[bt_kw_soversion] = tgt_dynamic_library,
		[bt_kw_vs_module_defs] = tgt_dynamic_library | tgt_static_library | tgt_shared_module,
	};

	uint32_t i;
	for (i = 0; i < bt_kwargs_count; ++i) {
		if (keyword_validity[i]
		    && akw[i].set
		    && !(keyword_validity[i] & argtype)) {
			interp_error(wk, akw[i].node, "invalid kwarg");
			return false;
		}
	}

	if (!typecheck_string_or_empty_array(wk, &akw[bt_kw_name_suffix])) {
		return false;
	} else if (!typecheck_string_or_empty_array(wk, &akw[bt_kw_name_prefix])) {
		return false;
	}

	bool multi_target = false;
	obj tgt = 0;

	for (i = 0; i <= tgt_type_count; ++i) {
		enum tgt_type t = 1 << i;

		if (!(type & t)) {
			continue;
		}

		if (tgt && !multi_target) {
			multi_target = true;
			make_obj(wk, res, obj_array);
			obj_array_push(wk, *res, tgt);

			// If this target is a multi-target (both_libraries),
			// clear out all sources arguments and set the objects
			// argument with objects from the previous target

			obj objects;
			if (!build_target_extract_all_objects(wk, an[0].node, tgt, &objects)) {
				return false;
			}

			akw[bt_kw_sources].set = false;
			get_obj_array(wk, an[1].val)->len = 0;

			if (akw[bt_kw_objects].set) {
				obj arr;
				obj_array_dup(wk, akw[bt_kw_objects].val, &arr);
				obj_array_extend(wk, objects, arr);
			} else {
				akw[bt_kw_objects].set = true;
				akw[bt_kw_objects].val = objects;
				akw[bt_kw_objects].node = an[0].node;
			}
		}

		if (!create_target(wk, an, akw, t, &tgt)) {
			return false;
		}

		if (multi_target) {
			obj_array_push(wk, *res, tgt);
		} else {
			*res = tgt;
		}
	}

	return true;
}

bool
func_executable(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, tgt_executable, tgt_executable, false);
}

bool
func_static_library(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, tgt_static_library, tgt_static_library, false);
}

bool
func_shared_library(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, tgt_dynamic_library, tgt_dynamic_library, false);
}

bool
func_both_libraries(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, tgt_static_library | tgt_dynamic_library,
		tgt_static_library | tgt_dynamic_library, false);
}

bool
func_library(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, default_library_type(wk),
		tgt_static_library | tgt_dynamic_library, false);
}

bool
func_shared_module(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, tgt_shared_module,
		tgt_shared_module, false);
}

bool
func_build_target(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, 0,
		tgt_executable | tgt_static_library | tgt_dynamic_library, true);
}
