#include "posix.h"

#include "functions/common.h"
#include "functions/compiler.h"
#include "interpreter.h"
#include "log.h"

struct func_compiler_get_supported_arguments_iter_ctx {
	uint32_t arr, node;
};

static enum iteration_result
func_compiler_get_supported_arguments_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct func_compiler_get_supported_arguments_iter_ctx *ctx = _ctx;

	if (!typecheck(wk, ctx->node, val_id, obj_string)) {
		return ir_err;
	}

	L(log_interp, "TODO: check '%s'", wk_objstr(wk, val_id));

	obj_array_push(wk, ctx->arr, val_id);

	return ir_cont;
}

static bool
func_compiler_get_supported_arguments(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_array);

	return obj_array_foreach(wk, an[0].val,
		&(struct func_compiler_get_supported_arguments_iter_ctx) {
		.arr = *obj,
		.node = an[0].node,
	}, func_compiler_get_supported_arguments_iter);
}

static bool
func_compiler_get_id(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, "gcc");
	return true;
}

const struct func_impl_name impl_tbl_compiler[] = {
	{ "get_supported_arguments", func_compiler_get_supported_arguments },
	{ "get_id", func_compiler_get_id },
	{ NULL, NULL },
};
