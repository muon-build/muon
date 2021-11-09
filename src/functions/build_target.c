#include "posix.h"

#include <string.h>

#include "coerce.h"
#include "functions/build_target.h"
#include "functions/common.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/path.h"

bool
tgt_build_path(struct workspace *wk, const struct obj *tgt, bool relative, char res[PATH_MAX])
{
	char tmp[PATH_MAX] = { 0 };
	if (!path_join(tmp, PATH_MAX, get_cstr(wk, tgt->dat.tgt.build_dir), get_cstr(wk, tgt->dat.tgt.build_name))) {
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
tgt_parts_dir(struct workspace *wk, const struct obj *tgt, bool relative, char res[PATH_MAX])
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
tgt_src_to_object_path(struct workspace *wk, const struct obj *tgt, obj src_file, bool relative, char res[PATH_MAX])
{
	struct obj *src = get_obj(wk, src_file);
	assert(get_obj(wk, src_file)->type == obj_file);

	char rel[PATH_MAX], parts_dir[PATH_MAX];
	const char *base;

	if (!tgt_parts_dir(wk, tgt, relative, parts_dir)) {
		return false;
	}

	if (path_is_subpath(get_cstr(wk, tgt->dat.tgt.build_dir), get_cstr(wk, src->dat.file))) {
		// file is a generated source
		base = get_cstr(wk, tgt->dat.tgt.build_dir);
	} else if (path_is_subpath(get_cstr(wk, tgt->dat.tgt.cwd),
		// file is in target cwd
		get_cstr(wk, src->dat.file))) {
		base = get_cstr(wk, tgt->dat.tgt.cwd);
	} else {
		// file is in source root
		base = wk->source_root;
	}

	if (!path_relative_to(rel, PATH_MAX, base, get_cstr(wk, src->dat.file))) {
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

	make_obj(wk, res, obj_string)->dat.str = get_obj(wk, rcvr)->dat.tgt.name;
	return true;
}

static bool
func_build_target_full_path(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	struct obj *tgt = get_obj(wk, rcvr);

	char path[PATH_MAX];
	if (!tgt_build_path(wk, tgt, false, path)) {
		return false;
	}

	*res = make_str(wk, path);
	return true;
}

struct build_target_extract_objects_ctx {
	uint32_t err_node;
	struct obj *tgt;
	obj *res;
};

static enum iteration_result
build_target_extract_objects_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct build_target_extract_objects_ctx *ctx = _ctx;

	struct obj *v = get_obj(wk, val);

	obj file;

	switch (v->type) {
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
		interp_error(wk, ctx->err_node, "expected string or file, got %s", obj_type_to_s(v->type));
		return ir_err;
	}

	if (!obj_array_in(wk, ctx->tgt->dat.tgt.src, file)) {
		interp_error(wk, ctx->err_node, "%o is not in target sources (%o)", file, ctx->tgt->dat.tgt.src);
		return ir_err;
	}

	enum compiler_language l;
	if (!filename_to_compiler_language(get_cstr(wk, get_obj(wk, val)->dat.file), &l)) {
		return ir_err;
	}

	switch (l) {
	case compiler_language_cpp_hdr:
	case compiler_language_c_hdr:
		return ir_cont;
	case compiler_language_c:
	case compiler_language_cpp:
	case compiler_language_c_obj:
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
	make_obj(wk, &new_file, obj_file)->dat.file = wk_str_push(wk, dest_path);
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
		.tgt = get_obj(wk, rcvr),
	};

	return obj_array_foreach_flat(wk, an[0].val, &ctx, build_target_extract_objects_iter);
}

static enum iteration_result
build_target_extract_all_objects_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct build_target_extract_objects_ctx *ctx = _ctx;
	assert(get_obj(wk, val)->type == obj_file);

	return build_target_extract_objects_iter(wk, ctx, val);
}

bool
build_target_extract_all_objects(struct workspace *wk, uint32_t err_node, obj rcvr, obj *res)
{
	make_obj(wk, res, obj_array);

	struct build_target_extract_objects_ctx ctx = {
		.err_node = err_node,
		.res = res,
		.tgt = get_obj(wk, rcvr),
	};

	return obj_array_foreach_flat(wk, ctx.tgt->dat.tgt.src, &ctx, build_target_extract_all_objects_iter);
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

	if (akw[kw_recursive].set && !get_obj(wk, akw[kw_recursive].val)->dat.boolean) {
		interp_error(wk, akw[kw_recursive].node, "non-recursive extract_all_objects not supported");
		return false;
	}

	return build_target_extract_all_objects(wk, rcvr, args_node, res);
}

const struct func_impl_name impl_tbl_build_target[] = {
	{ "extract_objects", func_build_target_extract_objects },
	{ "extract_all_objects", func_build_target_extract_all_objects },
	{ "full_path", func_build_target_full_path },
	{ "name", func_build_target_name },
	{ NULL, NULL },
};
