#include "posix.h"

#include <string.h>

#include "coerce.h"
#include "functions/build_target.h"
#include "functions/common.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/path.h"

bool
tgt_build_path(struct workspace *wk, const struct obj_build_target *tgt, bool relative, char res[PATH_MAX])
{
	char tmp[PATH_MAX] = { 0 };
	if (!path_join(tmp, PATH_MAX, get_cstr(wk, tgt->build_dir), get_cstr(wk, tgt->build_name))) {
		return false;
	}

	if (relative) {
		if (!path_relative_to(res, PATH_MAX, wk->build_root, tmp)) {
			return false;
		}
	} else {
		memcpy(res, tmp, PATH_MAX);
	}

	return true;
}

bool
tgt_parts_dir(struct workspace *wk, const struct obj_build_target *tgt, bool relative, char res[PATH_MAX])
{
	char build_path[PATH_MAX];
	if (!tgt_build_path(wk, tgt, relative, build_path)) {
		return false;
	}

	memcpy(res, build_path, PATH_MAX);
	if (!path_add_suffix(res, PATH_MAX, ".p")) {
		return false;
	}

	return true;
}

bool
tgt_src_to_object_path(struct workspace *wk, const struct obj_build_target *tgt, obj src_file, bool relative, char res[PATH_MAX])
{
	obj src = *get_obj_file(wk, src_file);

	char rel[PATH_MAX], parts_dir[PATH_MAX];
	const char *base;

	if (!tgt_parts_dir(wk, tgt, relative, parts_dir)) {
		return false;
	}

	if (path_is_subpath(get_cstr(wk, tgt->build_dir), get_cstr(wk, src))) {
		// file is a generated source
		base = get_cstr(wk, tgt->build_dir);
	} else if (path_is_subpath(get_cstr(wk, tgt->cwd),
		// file is in target cwd
		get_cstr(wk, src))) {
		base = get_cstr(wk, tgt->cwd);
	} else {
		// file is in source root
		base = wk->source_root;
	}

	if (!path_relative_to(rel, PATH_MAX, base, get_cstr(wk, src))) {
		return false;
	} else if (!path_join(res, PATH_MAX, parts_dir, rel)) {
		return false;
	} else if (!path_add_suffix(res, PATH_MAX, ".o")) {
		return false;
	}

	return true;
}

static bool
func_build_target_name(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_build_target(wk, rcvr)->name;
	return true;
}

static bool
func_build_target_full_path(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	struct obj_build_target *tgt = get_obj_build_target(wk, rcvr);

	char path[PATH_MAX];
	if (!tgt_build_path(wk, tgt, false, path)) {
		return false;
	}

	*res = make_str(wk, path);
	return true;
}

struct build_target_extract_objects_ctx {
	uint32_t err_node;
	struct obj_build_target *tgt;
	obj *res;
};

static enum iteration_result
build_target_extract_objects_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct build_target_extract_objects_ctx *ctx = _ctx;
	obj file;
	enum obj_type t = get_obj_type(wk, val);


	switch (t) {
	case obj_string: {
		if (!coerce_string_to_file(wk, val, &file)) {
			return ir_err;
		}
		break;
	}
	case obj_file:
		file = val;
		break;
	default:
		interp_error(wk, ctx->err_node, "expected string or file, got %s", obj_type_to_s(t));
		return ir_err;
	}

	if (!obj_array_in(wk, ctx->tgt->src, file)) {
		interp_error(wk, ctx->err_node, "%o is not in target sources (%o)", file, ctx->tgt->src);
		return ir_err;
	}

	enum compiler_language l;
	if (!filename_to_compiler_language(get_file_path(wk, file), &l)) {
		return ir_err;
	}

	switch (l) {
	case compiler_language_cpp_hdr:
	case compiler_language_c_hdr:
		return ir_cont;
	case compiler_language_c_obj:
		obj_array_push(wk, *ctx->res, file);
		return ir_cont;
	case compiler_language_c:
	case compiler_language_cpp:
		break;
	case compiler_language_count:
		assert(false && "unreachable");
		break;
	}

	char dest_path[PATH_MAX];
	if (!tgt_src_to_object_path(wk, ctx->tgt, file, false, dest_path)) {
		return ir_err;
	}

	obj new_file;
	make_obj(wk, &new_file, obj_file);
	*get_obj_file(wk, new_file) = make_str(wk, dest_path);
	obj_array_push(wk, *ctx->res, new_file);
	return ir_cont;
}

static bool
func_build_target_extract_objects(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_array);

	struct build_target_extract_objects_ctx ctx = {
		.err_node = an[0].node,
		.res = res,
		.tgt = get_obj_build_target(wk, rcvr),
	};

	return obj_array_foreach_flat(wk, an[0].val, &ctx, build_target_extract_objects_iter);
}

static enum iteration_result
build_target_extract_all_objects_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct build_target_extract_objects_ctx *ctx = _ctx;

	return build_target_extract_objects_iter(wk, ctx, val);
}

bool
build_target_extract_all_objects(struct workspace *wk, uint32_t err_node, obj rcvr, obj *res)
{
	make_obj(wk, res, obj_array);

	struct build_target_extract_objects_ctx ctx = {
		.err_node = err_node,
		.res = res,
		.tgt = get_obj_build_target(wk, rcvr),
	};

	return obj_array_foreach_flat(wk, ctx.tgt->src, &ctx, build_target_extract_all_objects_iter);
}

static bool
func_build_target_extract_all_objects(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	enum kwargs {
		kw_recursive,
	};
	struct args_kw akw[] = {
		[kw_recursive] = { "recursive", obj_bool },
		0
	};
	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	if (akw[kw_recursive].set && !get_obj_bool(wk, akw[kw_recursive].val)) {
		interp_error(wk, akw[kw_recursive].node, "non-recursive extract_all_objects not supported");
		return false;
	}

	return build_target_extract_all_objects(wk, args_node, rcvr, res);
}

const struct func_impl_name impl_tbl_build_target[] = {
	{ "extract_objects", func_build_target_extract_objects },
	{ "extract_all_objects", func_build_target_extract_all_objects },
	{ "full_path", func_build_target_full_path },
	{ "name", func_build_target_name },
	{ NULL, NULL },
};
