#include "posix.h"

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


struct coerce_into_files_ctx {
	uint32_t node;
	uint32_t arr;
	const char *type;
	exists_func exists;
};

static enum iteration_result
coerce_into_files_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct coerce_into_files_ctx *ctx = _ctx;

	switch (get_obj(wk, val)->type) {
	case obj_string: {
		uint32_t abs = wk_str_pushf(wk, "%s/%s", wk_str(wk, current_project(wk)->cwd), wk_objstr(wk, val));

		if (!ctx->exists(wk_str(wk, abs))) {
			interp_error(wk, ctx->node, "%s '%s' does not exist",
				ctx->type,
				wk_str(wk, abs));
			return false;
		}

		uint32_t file;
		make_obj(wk, &file, obj_file)->dat.file = abs;

		obj_array_push(wk, ctx->arr, file);
		break;
	}
	case obj_file:
		obj_array_push(wk, ctx->arr, val);
		break;
	default:
		interp_error(wk, ctx->node, "unable to coerce object with type %s into %s",
			obj_type_to_s(get_obj(wk, val)->type),
			ctx->type
			);
		return ir_err;
	}

	return ir_cont;
}

static bool
_coerce_files(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res, const char *type_name, exists_func exists)
{
	make_obj(wk, res, obj_array);

	struct coerce_into_files_ctx ctx = {
		.node = node,
		.arr = *res,
		.type = type_name,
		.exists = exists
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
coerce_files(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res)
{
	return _coerce_files(wk, node, val, res, "file", fs_file_exists);
}

bool
coerce_dirs(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res)
{
	return _coerce_files(wk, node, val, res, "directory", fs_dir_exists);
}
