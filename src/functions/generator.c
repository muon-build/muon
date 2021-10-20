#include "posix.h"

#include "functions/common.h"
#include "functions/default/custom_target.h"
#include "functions/generator.h"
#include "lang/interpreter.h"
#include "log.h"

struct generator_process_ctx {
	obj name;
	uint32_t node;
	struct obj *g;
	obj *res;
};

static enum iteration_result
generator_process_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct generator_process_ctx *ctx = _ctx;

	obj tgt;
	if (!make_custom_target(
		wk,
		make_str(wk, "<generated>"),
		ctx->node,
		ctx->node,
		ctx->node,
		val,
		ctx->g->dat.generator.output,
		ctx->g->dat.generator.raw_command,
		ctx->g->dat.generator.depfile,
		ctx->g->dat.generator.capture,
		&tgt
		)) {
		return ir_err;
	}

	obj_array_push(wk, *ctx->res, tgt);
	obj_array_push(wk, current_project(wk)->targets, tgt);
	return ir_cont;
}

static bool
func_generator_process(struct workspace *wk, obj gen, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB }, ARG_TYPE_NULL };
	enum kwargs {
		kw_extra_arguments, // ignored
		kw_preserve_path_from, // ignored
	};
	struct args_kw akw[] = {
		[kw_extra_arguments]    = { "extra_arguments", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_preserve_path_from] = { "preserve_path_from", obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	struct generator_process_ctx ctx = {
		.name = make_str(wk, "<generated source>"),
		.node = an[0].node,
		.g = get_obj(wk, gen),
		.res = res,
	};

	make_obj(wk, ctx.res, obj_array);
	return obj_array_foreach_flat(wk, an[0].val, &ctx, generator_process_iter);
}

const struct func_impl_name impl_tbl_generator[] = {
	{ "process", func_generator_process },
	{ NULL, NULL },
};
