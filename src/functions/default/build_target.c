#include "posix.h"

#include <assert.h>
#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "functions/build_target.h"
#include "functions/default/build_target.h"
#include "functions/default/options.h"
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
	bt_kw_link_whole, // TODO
	bt_kw_version,
	bt_kw_build_by_default, // TODO
	bt_kw_extra_files, // TODO
	bt_kw_target_type,
	bt_kw_name_prefix,
	bt_kw_name_suffix,
	bt_kw_soversion, // TODO
	bt_kw_link_depends, // TODO
	bt_kw_objects,

	/* lang args */
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

	struct obj *dep = get_obj(wk, val);

	switch (dep->type) {
	case obj_external_library:
		return ir_cont;
	case obj_dependency:
		break;
	default:
		interp_error(wk, ctx->node, "invalid dependency: %o", val);
		return ir_err;
	}

	if (dep->dat.dep.sources) {
		obj src;
		obj_array_dup(wk, dep->dat.dep.sources, &src);
		obj_array_extend(wk, ctx->src, src);
	}

	return ir_cont;
}

static enum tgt_type
default_library_type(struct workspace *wk)
{
	obj opt;
	bool r = get_option(wk, current_project(wk), "default_library", &opt);
	assert(r && "default_library option not set");

	if (wk_streql(get_str(wk, opt), &WKSTR("static"))) {
		return tgt_static_library;
	} else if (wk_streql(get_str(wk, opt), &WKSTR("shared"))) {
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

static bool
create_target(struct workspace *wk, struct args_norm *an, struct args_kw *akw, enum tgt_type type, obj *res)
{
	obj input;

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

	if (!coerce_files(wk, an[1].node, an[1].val, &input)) {
		return false;
	}

	const char *pref, *suff, *ver_suff = NULL;

	switch (type) {
	case tgt_executable:
		pref = "";
		suff = "";
		break;
	case tgt_static_library:
		pref = "lib";
		suff = ".a";
		break;
	case tgt_dynamic_library:
		pref = "lib";
		suff = ".so";
		if (akw[bt_kw_version].set) {
			ver_suff = get_cstr(wk, akw[bt_kw_version].val);
		} else if (akw[bt_kw_soversion].set) {
			ver_suff = get_cstr(wk, akw[bt_kw_soversion].val);
		}
		break;
	default:
		assert(false && "unreachable");
		return false;
	}

	if (akw[bt_kw_name_prefix].set) {
		pref = get_cstr(wk, akw[bt_kw_name_prefix].val);
	}

	if (akw[bt_kw_name_suffix].set) {
		suff = get_cstr(wk, akw[bt_kw_name_suffix].val);
	}

	struct obj *tgt = make_obj(wk, res, obj_build_target);
	tgt->dat.tgt.type = type;
	tgt->dat.tgt.name = get_obj(wk, an[0].val)->dat.str;
	tgt->dat.tgt.src = input;
	tgt->dat.tgt.build_name = wk_str_pushf(wk, "%s%s%s%s%s", pref, get_cstr(wk, tgt->dat.tgt.name), suff,
		ver_suff ? "." : "",
		ver_suff ? ver_suff : "");
	tgt->dat.tgt.cwd = current_project(wk)->cwd;
	tgt->dat.tgt.build_dir = current_project(wk)->build_dir;
	make_obj(wk, &tgt->dat.tgt.args, obj_dict);

	LOG_I("added target %s", get_cstr(wk, tgt->dat.tgt.build_name));

	if (type == tgt_dynamic_library) {
		char soversion[BUF_SIZE_1k] = { "." };
		bool have_soversion = false;

		if (akw[bt_kw_soversion].set) {
			have_soversion = true;
			strncpy(&soversion[1], get_cstr(wk, akw[bt_kw_soversion].val), BUF_SIZE_1k - 2);
		} else if (akw[bt_kw_version].set) {
			have_soversion = true;
			strncpy(&soversion[1], get_cstr(wk, akw[bt_kw_version].val), BUF_SIZE_1k - 2);
			char *p;
			if ((p = strchr(&soversion[1], '.'))) {
				*p = 0;
			}
		}

		char linker_name[BUF_SIZE_2k];
		snprintf(linker_name, BUF_SIZE_2k, "%s%s%s", pref, get_cstr(wk, tgt->dat.tgt.name), suff);

		tgt->dat.tgt.soname = wk_str_pushf(wk, "%s%s", linker_name, have_soversion ? soversion : "");

		char path[PATH_MAX];

		if (!fs_mkdir_p(get_cstr(wk, tgt->dat.tgt.build_dir))) {
			return false;
		}

		if (!wk_streql(get_str(wk, tgt->dat.tgt.build_name), get_str(wk, tgt->dat.tgt.soname))) {
			if (!path_join(path, PATH_MAX, get_cstr(wk, tgt->dat.tgt.build_dir),
				get_cstr(wk, tgt->dat.tgt.soname))) {
				return false;
			}

			if (!fs_make_symlink(get_cstr(wk, tgt->dat.tgt.build_name), path, true)) {
				return false;
			}
		}

		if (!wk_cstreql(get_str(wk, tgt->dat.tgt.soname), linker_name)) {
			if (!path_join(path, PATH_MAX, get_cstr(wk, tgt->dat.tgt.build_dir), linker_name)) {
				return false;
			}

			if (!fs_make_symlink(get_cstr(wk, tgt->dat.tgt.soname), path, true)) {
				return false;
			}
		}
	}

	{ // include directories
		obj inc_dirs;
		make_obj(wk, &inc_dirs, obj_array);
		uint32_t node = an[0].node; // TODO: not a very informative error node

		if (!(akw[bt_kw_implicit_include_directories].set
		      && !get_obj(wk, akw[bt_kw_implicit_include_directories].val)->dat.boolean)) {
			obj str;
			make_obj(wk, &str, obj_string)->dat.str = current_project(wk)->cwd;
			obj_array_push(wk, inc_dirs, str);
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

		tgt->dat.tgt.include_directories = coerced;
	}

	if (akw[bt_kw_dependencies].set) {
		tgt->dat.tgt.deps = akw[bt_kw_dependencies].val;
		struct add_dep_sources_ctx ctx = {
			.node = akw[bt_kw_dependencies].node,
			.src = tgt->dat.tgt.src,
		};

		obj_array_foreach(wk, tgt->dat.tgt.deps, &ctx, add_dep_sources_iter);
	}

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
			obj_dict_seti(wk, tgt->dat.tgt.args, lang_args[i].l, akw[bt_kw_c_args].val);
		}
	}

	if (akw[bt_kw_link_args].set) {
		tgt->dat.tgt.link_args = akw[bt_kw_link_args].val;
	}

	make_obj(wk, &tgt->dat.tgt.link_with, obj_array);
	if (akw[bt_kw_link_with].set) {
		obj arr;
		obj_array_dup(wk, akw[bt_kw_link_with].val, &arr);
		obj_array_extend(wk, tgt->dat.tgt.link_with, arr);
	}

	if (akw[bt_kw_link_whole].set) {
		obj arr;
		obj_array_dup(wk, akw[bt_kw_link_whole].val, &arr);
		obj_array_extend(wk, tgt->dat.tgt.link_with, arr);
	}

	if (akw[bt_kw_install].set && get_obj(wk, akw[bt_kw_install].val)->dat.boolean) {
		obj install_dir = 0;
		if (akw[bt_kw_install_dir].set) {
			install_dir = akw[bt_kw_install_dir].val;
		} else {
			switch (type) {
			case tgt_executable:
				if (!get_option(wk, current_project(wk), "bindir", &install_dir)) {
					return false;
				}
				break;
			case tgt_dynamic_library:
			case tgt_static_library:
				if (!get_option(wk, current_project(wk), "libdir", &install_dir)) {
					return false;
				}
				break;
			default:
				assert(false && "unreachable");
				break;
			}
		}

		obj install_mode_id = 0;
		if (akw[bt_kw_install_mode].set) {
			install_mode_id = akw[bt_kw_install_mode].val;
		}

		push_install_target_basename(wk, tgt->dat.tgt.build_dir,
			tgt->dat.tgt.build_name, install_dir, install_mode_id);
	}

	obj_array_push(wk, current_project(wk)->targets, *res);

	return true;
}

static bool
tgt_common(struct workspace *wk, uint32_t args_node, obj *res, enum tgt_type type, bool tgt_type_from_kw)
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
		[bt_kw_name_prefix] = { "name_prefix", obj_string },
		[bt_kw_name_suffix] = { "name_suffix", obj_string },
		[bt_kw_soversion] = { "soversion", obj_any },
		[bt_kw_link_depends] = { "link_depends", obj_any },
		[bt_kw_objects] = { "objects", ARG_TYPE_ARRAY_OF | obj_file },
		/* lang args */
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
	};

	uint32_t i;
	for (i = 0; i < bt_kwargs_count; ++i) {
		if (keyword_validity[i]
		    && akw[i].set
		    && !(keyword_validity[i] & type)) {
			interp_error(wk, akw[i].node, "invalid kwarg");
			return false;
		}
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
			get_obj(wk, an[1].val)->dat.arr.len = 0;

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
	return tgt_common(wk, args_node, res, tgt_executable, false);
}

bool
func_static_library(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, tgt_static_library, false);
}

bool
func_shared_library(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, tgt_dynamic_library, false);
}

bool
func_both_libraries(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, tgt_static_library | tgt_dynamic_library, false);
}

bool
func_library(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, default_library_type(wk), false);
}

bool
func_build_target(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, 0, true);
}
