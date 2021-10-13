#include "posix.h"

#include <assert.h>
#include <string.h>

#include "coerce.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

bool
coerce_string(struct workspace *wk, uint32_t node, obj val, obj *res)
{
	struct obj *v = get_obj(wk, val);
	switch (v->type) {
	case obj_bool:
		if (v->dat.boolean) {
			*res = make_str(wk, "true");
		} else {
			*res = make_str(wk, "false");
		}
		break;
	case obj_file:
		make_obj(wk, res, obj_string)->dat.str = v->dat.str;
		break;
	case obj_number: {
		make_obj(wk, res, obj_string)->dat.str = wk_str_pushf(wk, "%ld", v->dat.num);
		break;
	}
	case obj_string: {
		*res = val;
		break;
	}
	default:
		interp_error(wk, node, "unable to coerce %o to string", val);
		return false;
	}

	return true;
}

bool
coerce_executable(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res)
{
	struct obj *obj;
	uint32_t str;

	switch ((obj = get_obj(wk, val))->type) {
	case obj_file:
		str = get_obj(wk, val)->dat.file;
		break;
	case obj_build_target: {
		char tmp1[PATH_MAX], dest[PATH_MAX];

		if (!path_join(dest, PATH_MAX, get_cstr(wk, obj->dat.tgt.build_dir),
			get_cstr(wk, obj->dat.tgt.build_name))) {
			return false;
		} else if (!path_relative_to(tmp1, PATH_MAX, wk->build_root, dest)) {
			return false;
		} else if (!path_executable(dest, PATH_MAX, tmp1)) {
			return false;
		}

		str = wk_str_push(wk, dest);
		break;
	}
	case obj_external_program:
		str = obj->dat.external_program.full_path;
		break;
	default:
		interp_error(wk, node, "unable to coerce '%s' into executable", obj_type_to_s(obj->type));
		return false;
	}

	make_obj(wk, res, obj_string)->dat.str = str;
	return true;
}

bool
coerce_requirement(struct workspace *wk, struct args_kw *kw_required, enum requirement_type *requirement)
{
	if (kw_required->set) {
		if (get_obj(wk, kw_required->val)->type == obj_bool) {
			if (get_obj(wk, kw_required->val)->dat.boolean) {
				*requirement = requirement_required;
			} else {
				*requirement = requirement_auto;
			}
		} else if (get_obj(wk, kw_required->val)->type == obj_feature_opt) {
			switch (get_obj(wk, kw_required->val)->dat.feature_opt.state) {
			case feature_opt_disabled:
				*requirement = requirement_skip;
				break;
			case feature_opt_enabled:
				*requirement = requirement_required;
				break;
			case feature_opt_auto:
				*requirement = requirement_auto;
				break;
			}
		} else {
			interp_error(wk, kw_required->node, "expected type %s or %s, got %s",
				obj_type_to_s(obj_bool),
				obj_type_to_s(obj_feature_opt),
				obj_type_to_s(get_obj(wk, kw_required->val)->type)
				);
			return false;
		}
	} else {
		*requirement = requirement_required;
	}

	return true;
}

typedef bool (*exists_func)(const char *);

enum coerce_into_files_mode {
	mode_input,
	mode_output,
};

struct coerce_into_files_ctx {
	uint32_t node;
	uint32_t arr;
	const char *type;
	exists_func exists;
	enum coerce_into_files_mode mode;
};

static enum iteration_result
coerce_custom_target_output_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct coerce_into_files_ctx *ctx = _ctx;
	assert(get_obj(wk, val)->type == obj_file);

	obj_array_push(wk, ctx->arr, val);
	return ir_cont;
}

static enum iteration_result
coerce_into_files_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct coerce_into_files_ctx *ctx = _ctx;

	switch (get_obj(wk, val)->type) {
	case obj_string: {
		uint32_t path;
		char buf[PATH_MAX];

		switch (ctx->mode) {
		case mode_input:
			if (path_is_absolute(get_cstr(wk, val))) {
				path = get_obj(wk, val)->dat.str;
			} else {
				if (!path_join(buf, PATH_MAX, get_cstr(wk, current_project(wk)->cwd), get_cstr(wk, val))) {
					return ir_err;
				}

				path = wk_str_push(wk, buf);
			}

			if (!ctx->exists(get_cstr(wk, path))) {
				interp_error(wk, ctx->node, "%s '%s' does not exist",
					ctx->type, get_cstr(wk, path));
				return ir_err;
			}
			break;
		case mode_output:
			if (!path_is_basename(get_cstr(wk, val))) {
				interp_error(wk, ctx->node, "output file '%s' contains path seperators", get_cstr(wk, val));
				return ir_err;
			}

			if (!path_join(buf, PATH_MAX, get_cstr(wk, current_project(wk)->build_dir), get_cstr(wk, val))) {
				return ir_err;
			}

			path = wk_str_push(wk, buf);
			break;
		default:
			assert(false);
			return ir_err;
		}

		uint32_t file;
		make_obj(wk, &file, obj_file)->dat.file = path;
		obj_array_push(wk, ctx->arr, file);
		break;
	}
	case obj_custom_target: {
		if (!obj_array_foreach(wk, get_obj(wk, val)->dat.custom_target.output,
			ctx, coerce_custom_target_output_iter)) {
			return ir_err;
		}
		break;
	}
	case obj_build_target: {
		struct obj *tgt = get_obj(wk, val);

		char path[PATH_MAX];
		if (!path_join(path, PATH_MAX, get_cstr(wk, tgt->dat.tgt.build_dir), get_cstr(wk, tgt->dat.tgt.build_name))) {
			return ir_err;
		}

		uint32_t file;
		make_obj(wk, &file, obj_file)->dat.file = wk_str_push(wk, path);
		obj_array_push(wk, ctx->arr, file);
		break;
	}
	case obj_file:
		if (ctx->mode == mode_output) {
			goto type_error;
		}
		obj_array_push(wk, ctx->arr, val);
		break;
	default:
type_error:
		interp_error(wk, ctx->node, "unable to coerce object with type %s into %s",
			obj_type_to_s(get_obj(wk, val)->type), ctx->type);
		return ir_err;
	}

	return ir_cont;
}

static bool
_coerce_files(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res,
	const char *type_name, exists_func exists, enum coerce_into_files_mode mode)
{
	make_obj(wk, res, obj_array);

	struct coerce_into_files_ctx ctx = {
		.node = node,
		.arr = *res,
		.type = type_name,
		.exists = exists,
		.mode = mode,
	};

	switch (get_obj(wk, val)->type) {
	case obj_array:
		return obj_array_foreach_flat(wk, val, &ctx, coerce_into_files_iter);
	default:
		switch (coerce_into_files_iter(wk, &ctx, val)) {
		case ir_err:
			return false;
		default:
			return true;
		}
	}
}

bool
coerce_output_files(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res)
{
	return _coerce_files(wk, node, val, res, "output file", NULL, mode_output);
}

bool
coerce_files(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res)
{
	return _coerce_files(wk, node, val, res, "file", fs_file_exists, mode_input);
}

bool
coerce_dirs(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res)
{
	return _coerce_files(wk, node, val, res, "directory", fs_dir_exists, mode_input);
}

struct include_directories_iter_ctx {
	uint32_t node;
	obj res;
	bool is_system;
};

static enum iteration_result
include_directories_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct include_directories_iter_ctx *ctx = _ctx;
	struct obj *d = get_obj(wk, v);

	if (d->type == obj_include_directory) {
		obj_array_push(wk, ctx->res, v);
		return ir_cont;
	} else if (d->type != obj_string) {
		interp_error(wk, ctx->node, "unable to coerce %o to include_directory", v);
		return ir_err;
	}

	str path = d->dat.str;
	char buf1[PATH_MAX], buf2[PATH_MAX];
	const char *p = get_cstr(wk, path);

	if (!path_is_absolute(p)) {
		if (!path_join(buf1, PATH_MAX, get_cstr(wk, current_project(wk)->cwd), p)) {
			return ir_err;
		}

		path = wk_str_push(wk, buf1);
	}

	p = get_cstr(wk, path);

	if (!fs_dir_exists(p)) {
		interp_error(wk, ctx->node, "directory '%s' does not exist", get_cstr(wk, path));
		return ir_err;
	}

	obj inc;
	d = make_obj(wk, &inc, obj_include_directory);
	d->dat.include_directory.path = path;
	d->dat.include_directory.is_system = ctx->is_system;
	obj_array_push(wk, ctx->res, inc);

	if (path_is_subpath(wk->source_root, p)) {
		if (!path_relative_to(buf1, PATH_MAX, wk->source_root, p)) {
			return ir_err;
		} else if (!path_join(buf2, PATH_MAX, wk->build_root, buf1)) {
			return ir_err;
		}

		d = make_obj(wk, &inc, obj_include_directory);
		d->dat.include_directory.path = wk_str_push(wk, buf2);
		d->dat.include_directory.is_system = ctx->is_system;
		obj_array_push(wk, ctx->res, inc);
	}

	return ir_cont;
}

bool
coerce_include_dirs(struct workspace *wk, uint32_t node, obj val, bool is_system, obj *res)
{
	struct include_directories_iter_ctx ctx = {
		.node = node,
		.is_system = is_system,
	};

	make_obj(wk, &ctx.res, obj_array);
	if (!obj_array_foreach_flat(wk, val, &ctx, include_directories_iter)) {
		return false;
	}

	*res = ctx.res;
	return true;
}
