/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "coerce.h"
#include "error.h"
#include "functions/build_target.h"
#include "functions/generator.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/path.h"

struct tgt_src_to_compiled_path_opts {
	bool relative;
	const char *default_ext;
	compiler_get_arg_func_0 get_ext;
	enum compiler_language lang;
};

static bool
tgt_src_to_compiled_path(struct workspace *wk,
	const struct obj_build_target *tgt,
	struct tgt_src_to_compiled_path_opts *opts,
	obj src_file,
	struct tstr *res)
{
	obj src = *get_obj_file(wk, src_file);

	TSTR(private_path_rel);
	TSTR(rel);
	const char *base, *private_path = get_cstr(wk, tgt->private_path);

	if (opts->relative) {
		path_relative_to(wk, &private_path_rel, wk->build_root, private_path);
		private_path = private_path_rel.buf;
	}

	if (path_is_subpath(get_cstr(wk, tgt->private_path), get_cstr(wk, src))) {
		// file is a source from a generated list
		base = get_cstr(wk, tgt->private_path);
	} else if (path_is_subpath(get_cstr(wk, tgt->build_dir), get_cstr(wk, src))) {
		// file is a generated source from custom_target / configure_file
		base = get_cstr(wk, tgt->build_dir);
	} else if (path_is_subpath(get_cstr(wk, tgt->cwd), get_cstr(wk, src))) {
		// file is in target cwd
		base = get_cstr(wk, tgt->cwd);
	} else if (path_is_subpath(wk->source_root, get_cstr(wk, src))) {
		// file is in source root
		base = wk->source_root;
	} else {
		// outside the source root
		base = NULL;
	}

	if (base) {
		path_relative_to(wk, &rel, base, get_cstr(wk, src));
	} else {
		path_copy(wk, &rel, get_cstr(wk, src));

		uint32_t i;
		for (i = 0; i < rel.len; ++i) {
			if (rel.buf[i] == PATH_SEP || rel.buf[i] == ':') {
				rel.buf[i] = '_';
			}
		}
	}

	path_join(wk, res, private_path, rel.buf);

	const char *ext = opts->default_ext;

	{
		obj comp_id;
		if (obj_dict_geti(wk, current_project(wk)->toolchains[tgt->machine], opts->lang, &comp_id)) {
			ext = opts->get_ext(wk, get_obj_compiler(wk, comp_id))->args[0];
		}
	}

	tstr_pushs(wk, res, ext);
	return true;
}

bool
tgt_src_to_pch_path(struct workspace *wk,
	const struct obj_build_target *tgt,
	enum compiler_language lang,
	obj src_file,
	struct tstr *res)
{
	struct tgt_src_to_compiled_path_opts opts = {
		.relative = true,
		.get_ext = toolchain_compiler_pch_ext,
		.lang = lang,
	};

	if (get_obj_type(wk, src_file) == obj_build_target) {
		tgt = get_obj_build_target(wk, src_file);
		obj tgt_pch = tgt->pch;
		if (!obj_dict_geti(wk, tgt_pch, lang, &src_file)) {
			UNREACHABLE;
		}
	}

	return tgt_src_to_compiled_path(wk, tgt, &opts, src_file, res);
}

bool
tgt_src_to_object_path(struct workspace *wk,
	const struct obj_build_target *tgt,
	enum compiler_language lang,
	obj src_file,
	bool relative,
	struct tstr *res)
{
	struct tgt_src_to_compiled_path_opts opts = {
		.relative = relative,
		.default_ext = ".o",
		.get_ext = toolchain_compiler_object_ext,
		.lang = lang,
	};

	return tgt_src_to_compiled_path(wk, tgt, &opts, src_file, res);
}

static bool
func_build_target_name(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_build_target(wk, self)->name;
	return true;
}

static bool
func_build_target_full_path(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct obj_build_target *tgt = get_obj_build_target(wk, self);

	*res = tgt->build_path;
	return true;
}

struct build_target_extract_objects_ctx {
	struct obj_build_target *tgt;
	obj tgt_id;
	obj *res;
	bool verify_exists;
};

static bool
build_target_extract_object(struct workspace *wk, struct build_target_extract_objects_ctx *ctx, obj val)
{
	obj file;
	enum obj_type t = get_obj_type(wk, val);

	if (!typecheck(wk, 0, val, tc_file | tc_string | tc_custom_target | tc_generated_list)) {
		return false;
	}

	switch (t) {
	case obj_string: {
		if (!coerce_string_to_file(wk, get_cstr(wk, ctx->tgt->cwd), val, &file)) {
			return false;
		}
		break;
	}
	case obj_file: file = val; break;
	case obj_custom_target: {
		struct obj_custom_target *tgt = get_obj_custom_target(wk, val);

		obj v;
		obj_array_for(wk, tgt->output, v) {
			if (!build_target_extract_object(wk, ctx, v)) {
				return false;
			}
		}
		return true;
	}
	case obj_generated_list: {
		obj processed;
		if (!generated_list_process_for_target(wk, 0, val, ctx->tgt_id, false, &processed)) {
			return false;
		}

		obj v;
		obj_array_for(wk, processed, v) {
			if (!build_target_extract_object(wk, ctx, v)) {
				return false;
			}
		}
		return true;
	}
	default: UNREACHABLE_RETURN;
	}

	enum compiler_language l;
	if (!filename_to_compiler_language(get_file_path(wk, file), &l)) {
		return false;
	}

	switch (l) {
	case compiler_language_c_hdr:
	case compiler_language_cpp_hdr:
	case compiler_language_objc_hdr:
	case compiler_language_objcpp_hdr:
	case compiler_language_c_obj:
		// skip non-compileable sources
		return true;
	case compiler_language_assembly:
	case compiler_language_nasm:
	case compiler_language_c:
	case compiler_language_cpp:
	case compiler_language_llvm_ir:
	case compiler_language_objcpp:
	case compiler_language_objc: break;
	case compiler_language_null:
	case compiler_language_count: UNREACHABLE;
	}

	if (ctx->verify_exists && !obj_array_in(wk, ctx->tgt->src, file)) {
		vm_error_at(wk, 0, "%o is not in target sources (%o)", file, ctx->tgt->src);
		return false;
	}

	TSTR(dest_path);
	if (!tgt_src_to_object_path(wk, ctx->tgt, l, file, false, &dest_path)) {
		return false;
	}

	obj new_file;
	new_file = make_obj(wk, obj_file);
	*get_obj_file(wk, new_file) = tstr_into_str(wk, &dest_path);
	obj_array_push(wk, *ctx->res, new_file);
	return ir_cont;
}

static bool
func_build_target_extract_objects(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[]
		= { { TYPE_TAG_GLOB | tc_string | tc_file | tc_custom_target | tc_generated_list }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = make_obj(wk, obj_array);

	struct build_target_extract_objects_ctx ctx = {
		.res = res,
		.tgt = get_obj_build_target(wk, self),
		.tgt_id = self,
		.verify_exists = true,
	};

	obj v;
	obj_array_for(wk, an[0].val, v) {
		if (!build_target_extract_object(wk, &ctx, v)) {
			return false;
		}
	}

	return true;
}

static bool build_target_extract_all_objects_impl(struct workspace *wk, obj self, obj *res, bool recursive);

static bool
build_targets_extract_all_objects(struct workspace *wk, obj arr, obj *res)
{
	if (!arr) {
		return true;
	}

	obj v;
	obj_array_for(wk, arr, v) {
		if (get_obj_type(wk, v) != obj_build_target) {
			continue;
		}

		if (!build_target_extract_all_objects_impl(wk, v, res, true)) {
			return false;
		}
	}

	return true;
}

static bool
build_target_extract_all_objects_recurse(struct workspace *wk, struct build_dep *dep, obj *res)
{
	if (!build_targets_extract_all_objects(wk, dep->raw.link_with, res)) {
		return false;
	}

	if (!build_targets_extract_all_objects(wk, dep->raw.link_whole, res)) {
		return false;
	}

	if (dep->raw.deps) {
		obj v;
		obj_array_for(wk, dep->raw.deps, v) {
			struct obj_dependency *dep = get_obj_dependency(wk, v);
			if (!build_target_extract_all_objects_recurse(wk, &dep->dep, res)) {
				return false;
			}
		}
	}

	return true;
}

static bool
build_target_extract_all_objects_impl(struct workspace *wk, obj self, obj *res, bool recursive)
{
	struct build_target_extract_objects_ctx ctx = {
		.res = res,
		.tgt = get_obj_build_target(wk, self),
		.tgt_id = self,
	};

	obj v;
	obj_array_for(wk, ctx.tgt->src, v) {
		if (!build_target_extract_object(wk, &ctx, v)) {
			return false;
		}
	}

	if (recursive) {
		obj_array_extend(wk, *res, ctx.tgt->objects);
		if (!build_target_extract_all_objects_recurse(wk, &ctx.tgt->dep_internal, res)) {
			return false;
		}
	}

	return true;
}

bool
build_target_extract_all_objects(struct workspace *wk, uint32_t ip, obj self, obj *res, bool recursive)
{
	*res = make_obj(wk, obj_array);
	return build_target_extract_all_objects_impl(wk, self, res, recursive);
}

static bool
func_build_target_extract_all_objects(struct workspace *wk, obj self, obj *res)
{
	enum kwargs {
		kw_recursive,
	};
	struct args_kw akw[] = { [kw_recursive] = { "recursive", obj_bool }, 0 };
	if (!pop_args(wk, NULL, akw)) {
		return false;
	}

	bool recursive = akw[kw_recursive].set ? get_obj_bool(wk, akw[kw_recursive].val) : false;

	return build_target_extract_all_objects(wk, 0, self, res, recursive);
}

static bool
func_build_target_private_dir_include(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj(wk, obj_include_directory);
	struct obj_include_directory *inc = get_obj_include_directory(wk, *res);

	inc->path = get_obj_build_target(wk, self)->private_path;
	return true;
}

static bool
func_build_target_found(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, true);
	return true;
}

const struct func_impl impl_tbl_build_target[] = {
	{ "extract_all_objects", func_build_target_extract_all_objects, tc_array },
	{ "extract_objects", func_build_target_extract_objects, tc_array },
	{ "found", func_build_target_found, tc_bool },
	{ "full_path", func_build_target_full_path, tc_string },
	{ "name", func_build_target_name, tc_string },
	{ "path", func_build_target_full_path, tc_string },
	{ "private_dir_include", func_build_target_private_dir_include, tc_string },
	{ NULL, NULL },
};
