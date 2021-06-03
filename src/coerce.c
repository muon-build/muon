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

struct coerce_into_files_ctx {
	struct args_kw *arg;
	uint32_t arr;
};

static enum iteration_result
coerce_into_files_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct coerce_into_files_ctx *ctx = _ctx;

	switch (get_obj(wk, val)->type) {
	case obj_string: {
		uint32_t abs = wk_str_pushf(wk, "%s/%s", wk_str(wk, current_project(wk)->cwd), wk_objstr(wk, val));

		if (!fs_file_exists(wk_str(wk, abs))) {
			interp_error(wk, ctx->arg->node, "file '%s' does not exist",
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
		interp_error(wk, ctx->arg->node, "unable to coerce object with type %s into file",
			obj_type_to_s(get_obj(wk, val)->type));
		return ir_err;
	}

	return ir_cont;
}

bool
coerce_files(struct workspace *wk, struct args_kw *arg, uint32_t *res)
{
	make_obj(wk, res, obj_array);

	struct coerce_into_files_ctx ctx = { .arg = arg, .arr = *res, };

	switch (get_obj(wk, arg->val)->type) {
	case obj_array:
		return obj_array_foreach(wk, arg->val, &ctx, coerce_into_files_iter);
	default:
		switch (coerce_into_files_iter(wk, &ctx, arg->val)) {
		case ir_err:
			return false;
		default:
			return true;
		}
	}
}
