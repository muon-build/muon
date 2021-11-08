#include "posix.h"

#include <assert.h>
#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "functions/default/build_target.h"
#include "functions/default/options.h"
#include "lang/interpreter.h"
#include "log.h"

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


static bool
tgt_common(struct workspace *wk, uint32_t args_node, obj *res, enum tgt_type type, bool tgt_type_from_kw)
{
	struct args_norm an[] = { { obj_string }, { ARG_TYPE_GLOB }, ARG_TYPE_NULL };
	enum kwargs {
		kw_sources,
		kw_include_directories,
		kw_implicit_include_directories,
		kw_dependencies,
		kw_install,
		kw_install_dir,
		kw_install_mode,
		kw_link_with,
		kw_link_whole, // TODO
		kw_version,
		kw_build_by_default, // TODO
		kw_extra_files, // TODO
		kw_target_type,
		kw_name_prefix,
		kw_name_suffix,
		kw_soversion, // TODO
		kw_link_depends, // TODO
		kw_objects,

		kw_c_args,
		kw_cpp_args,
		kw_objc_args,
		kw_link_args,
	};
	struct args_kw akw[] = {
		[kw_sources] = { "sources", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_include_directories] = { "include_directories", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_implicit_include_directories] = { "implicit_include_directories", obj_bool },
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_install] = { "install", obj_bool },
		[kw_install_dir] = { "install_dir", obj_string },
		[kw_install_mode] = { "install_mode", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_link_with] = { "link_with", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_link_whole] = { "link_whole", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_version] = { "version", obj_string },
		[kw_build_by_default] = { "build_by_default", obj_bool },
		[kw_extra_files] = { "extra_files", obj_any }, // ignored
		[kw_target_type] = { "target_type", obj_string },
		[kw_name_prefix] = { "name_prefix", obj_string },
		[kw_name_suffix] = { "name_suffix", obj_string },
		[kw_soversion] = { "soversion", obj_any },
		[kw_link_depends] = { "link_depends", obj_any },
		[kw_objects] = { "objects", ARG_TYPE_ARRAY_OF | obj_file },
		/* lang args */
		[kw_c_args] = { "c_args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_cpp_args] = { "cpp_args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_objc_args] = { "objc_args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_link_args] = { "link_args", ARG_TYPE_ARRAY_OF | obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (tgt_type_from_kw) {
		if (!akw[kw_target_type].set) {
			interp_error(wk, args_node, "missing required kwarg: %s", akw[kw_target_type].key);
			return false;
		}

		const char *tgt_type = get_cstr(wk, akw[kw_target_type].val);
		static struct { char *name; enum tgt_type type; } tgt_tbl[] = {
			{ "executable", tgt_executable, },
			{ "shared_library", tgt_library, },
			{ "static_library", tgt_library, },
			{ "both_libraries", tgt_library, },
			{ "library", tgt_library, },
			{ 0 },
		};
		uint32_t i;

		for (i = 0; tgt_tbl[i].name; ++i) {
			if (strcmp(tgt_type, tgt_tbl[i].name) == 0) {
				type = tgt_tbl[i].type;
				break;
			}
		}

		if (!tgt_tbl[i].name) {
			interp_error(wk, akw[kw_target_type].node, "unsupported target type '%s'", tgt_type);
			return false;
		}
	} else {
		if (akw[kw_target_type].set) {
			interp_error(wk, akw[kw_target_type].node, "invalid kwarg");
			return false;
		}
	}

	obj input;

	if (akw[kw_sources].set) {
		obj arr;
		obj_array_dup(wk, akw[kw_sources].val, &arr);
		obj_array_extend(wk, an[1].val, arr);
	}

	if (akw[kw_objects].set) {
		obj arr;
		obj_array_dup(wk, akw[kw_objects].val, &arr);
		obj_array_extend(wk, an[1].val, arr);
	}

	if (!coerce_files(wk, an[1].node, an[1].val, &input)) {
		return false;
	}

	const char *pref, *suff;

	switch (type) {
	case tgt_executable:
		pref = "";
		suff = "";
		break;
	case tgt_library:
		pref = "lib";
		suff = ".a";
		break;
	default:
		assert(false);
		return false;
	}

	if (akw[kw_name_prefix].set) {
		pref = get_cstr(wk, akw[kw_name_prefix].val);
	}

	if (akw[kw_name_suffix].set) {
		suff = get_cstr(wk, akw[kw_name_suffix].val);
	}

	struct obj *tgt = make_obj(wk, res, obj_build_target);
	tgt->dat.tgt.type = type;
	tgt->dat.tgt.name = get_obj(wk, an[0].val)->dat.str;
	tgt->dat.tgt.src = input;
	tgt->dat.tgt.build_name = wk_str_pushf(wk, "%s%s%s", pref, get_cstr(wk, tgt->dat.tgt.name), suff);
	tgt->dat.tgt.cwd = current_project(wk)->cwd;
	tgt->dat.tgt.build_dir = current_project(wk)->build_dir;
	make_obj(wk, &tgt->dat.tgt.args, obj_dict);

	LOG_I("added target %s", get_cstr(wk, tgt->dat.tgt.build_name));

	{ // include directories
		obj inc_dirs;
		make_obj(wk, &inc_dirs, obj_array);
		uint32_t node = args_node;

		if (!(akw[kw_implicit_include_directories].set
		      && !get_obj(wk, akw[kw_implicit_include_directories].val)->dat.boolean)) {
			obj str;
			make_obj(wk, &str, obj_string)->dat.str = current_project(wk)->cwd;
			obj_array_push(wk, inc_dirs, str);
		}

		if (akw[kw_include_directories].set) {
			node = akw[kw_include_directories].node;

			obj inc;
			obj_array_dup(wk, akw[kw_include_directories].val, &inc);
			obj_array_extend(wk, inc_dirs, inc);
		}

		obj coerced;
		if (!coerce_include_dirs(wk, node, inc_dirs, false, &coerced)) {
			return false;
		}

		tgt->dat.tgt.include_directories = coerced;
	}

	if (akw[kw_dependencies].set) {
		tgt->dat.tgt.deps = akw[kw_dependencies].val;
		struct add_dep_sources_ctx ctx = {
			.node = akw[kw_dependencies].node,
			.src = tgt->dat.tgt.src,
		};

		obj_array_foreach(wk, tgt->dat.tgt.deps, &ctx, add_dep_sources_iter);
	}

	static struct {
		enum kwargs kw;
		enum compiler_language l;
	} lang_args[] = {
		{ kw_c_args, compiler_language_c },
		{ kw_cpp_args, compiler_language_cpp },
		/* { kw_objc_args, compiler_language_objc }, */
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(lang_args); ++i) {
		if (akw[lang_args[i].kw].set) {
			obj_dict_seti(wk, tgt->dat.tgt.args, lang_args[i].l, akw[kw_c_args].val);
		}
	}

	if (akw[kw_link_args].set) {
		tgt->dat.tgt.link_args = akw[kw_link_args].val;
	}

	make_obj(wk, &tgt->dat.tgt.link_with, obj_array);
	if (akw[kw_link_with].set) {
		obj arr;
		obj_array_dup(wk, akw[kw_link_with].val, &arr);
		obj_array_extend(wk, tgt->dat.tgt.link_with, arr);
	}

	if (akw[kw_link_whole].set) {
		obj arr;
		obj_array_dup(wk, akw[kw_link_whole].val, &arr);
		obj_array_extend(wk, tgt->dat.tgt.link_with, arr);
	}

	if (akw[kw_install].set && get_obj(wk, akw[kw_install].val)->dat.boolean) {
		obj install_dir = 0;
		if (akw[kw_install_dir].set) {
			install_dir = akw[kw_install_dir].val;
		} else {
			switch (type) {
			case tgt_executable:
				if (!get_option(wk, current_project(wk), "bindir", &install_dir)) {
					return false;
				}
				break;
			case tgt_library:
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
		if (akw[kw_install_mode].set) {
			install_mode_id = akw[kw_install_mode].val;
		}

		push_install_target_basename(wk, tgt->dat.tgt.build_dir,
			tgt->dat.tgt.build_name, install_dir, install_mode_id);
	}

	obj_array_push(wk, current_project(wk)->targets, *res);

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
	return tgt_common(wk, args_node, res, tgt_library, false);
}

bool
func_build_target(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	return tgt_common(wk, args_node, res, 0, true);
}

