#include "posix.h"

#include "functions/common.h"
#include "functions/compiler.h"
#include "interpreter.h"
#include "log.h"

static enum iteration_result
func_compiler_get_supported_arguments_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	uint32_t *arr = _ctx;
	struct obj *val = get_obj(wk, val_id);

	if (!typecheck(val, obj_string)) {
		return ir_err;
	}

	L(log_interp, "TODO: check '%s'", wk_objstr(wk, val_id));

	obj_array_push(wk, *arr, val_id);

	return ir_cont;
}

static bool
func_compiler_get_supported_arguments(struct ast *ast, struct workspace *wk,
	uint32_t _, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_array);

	return obj_array_foreach(wk, an[0].val, obj, func_compiler_get_supported_arguments_iter);
}

const struct func_impl_name impl_tbl_compiler[] = {
	{ "get_supported_arguments", func_compiler_get_supported_arguments },
	{ NULL, NULL },
};
