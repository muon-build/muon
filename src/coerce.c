#include "posix.h"

#include <assert.h>
#include <string.h>

#include "coerce.h"
#include "filesystem.h"
#include "interpreter.h"

bool
coerce_requirement(struct workspace *wk, struct args_kw *kw_required,
	enum requirement_type *requirement)
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

		switch (ctx->mode) {
		case mode_input:
			if (*wk_objstr(wk, val) == '/') {
				path = get_obj(wk, val)->dat.str;
			} else {
				path = wk_str_pushf(wk, "%s/%s", wk_str(wk, current_project(wk)->cwd), wk_objstr(wk, val));
			}

			if (!ctx->exists(wk_str(wk, path))) {
				interp_error(wk, ctx->node, "%s '%s' does not exist",
					ctx->type,
					wk_str(wk, path));
				return ir_err;
			}
			break;
		case mode_output:
			if (strchr(wk_objstr(wk, val), '/')) {
				interp_error(wk, ctx->node, "output files may not contain '/'");
				return ir_err;
			}

			if (*wk_objstr(wk, val) == '/') {
				path = get_obj(wk, val)->dat.str;
			} else {
				path = wk_str_pushf(wk, "%s/%s", wk_str(wk, current_project(wk)->build_dir), wk_objstr(wk, val));
			}
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
	case obj_file:
		if (ctx->mode == mode_output) {
			goto type_error;
		}
		obj_array_push(wk, ctx->arr, val);
		break;
	default:
type_error:
		interp_error(wk, ctx->node, "unable to coerce object with type %s into %s",
			obj_type_to_s(get_obj(wk, val)->type),
			ctx->type
			);
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
