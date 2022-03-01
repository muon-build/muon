#include "posix.h"

#include "coerce.h"
#include "functions/build_target.h"
#include "functions/common.h"
#include "functions/default/custom_target.h"
#include "functions/generator.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/path.h"

struct generated_list_process_for_target_ctx {
	uint32_t node;
	struct obj_generator *g;
	struct obj_generated_list *gl;
	const char *dir;
	bool add_targets, generated_include;
	obj *res;
};

static enum iteration_result
generated_list_process_for_target_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct generated_list_process_for_target_ctx *ctx = _ctx;

	obj tgt;
	if (!make_custom_target(
		wk,
		0,
		ctx->node,
		ctx->node,
		ctx->node,
		val,
		ctx->g->output,
		ctx->dir,
		ctx->g->raw_command,
		ctx->g->depfile,
		ctx->g->capture,
		&tgt
		)) {
		return ir_err;
	}

	struct obj_custom_target *t = get_obj_custom_target(wk, tgt);

	obj file;
	obj_array_flatten_one(wk, t->output, &file);
	obj_array_push(wk, *ctx->res, file);

	if (ctx->add_targets) {
		const char *generated_path = get_cstr(wk, *get_obj_file(wk, file));

		enum compiler_language l;
		if (!ctx->generated_include
		    && filename_to_compiler_language(generated_path, &l)
		    && languages[l].is_header) {
			ctx->generated_include = true;
		}

		char rel[PATH_MAX];
		if (!path_relative_to(rel, PATH_MAX, wk->build_root, generated_path)) {
		}

		t->name = make_strf(wk, "<generated %s>", rel);
		if (ctx->g->depends) {
			obj_array_extend(wk, t->depends, ctx->g->depends);
		}
		obj_array_push(wk, current_project(wk)->targets, tgt);
	}

	return ir_cont;
}

bool
generated_list_process_for_target(struct workspace *wk, uint32_t err_node,
	obj gl, obj tgt, bool add_targets, obj *res)
{
	struct obj_generated_list *list = get_obj_generated_list(wk, gl);

	enum obj_type t = get_obj_type(wk, tgt);

	const char *private_path;

	switch (t) {
	case obj_build_target:
		private_path = get_cstr(wk, get_obj_build_target(wk, tgt)->private_path);
		break;
	case obj_custom_target: {
		private_path = get_cstr(wk, get_obj_custom_target(wk, tgt)->private_path);
		break;
	}
	default:
		assert(false && "invalid tgt type");
		return false;
	}

	make_obj(wk, res, obj_array);

	struct generated_list_process_for_target_ctx ctx = {
		.node = err_node,
		.g = get_obj_generator(wk, list->generator),
		.gl = list,
		.dir = private_path,
		.add_targets = add_targets,
		.res = res,
	};

	if (add_targets && t == obj_build_target) {
		get_obj_build_target(wk, tgt)->flags |= build_tgt_generated_include;
	}

	if (!obj_array_foreach(wk, list->input, &ctx, generated_list_process_for_target_iter)) {
		return false;
	}

	return true;
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
		[kw_extra_arguments] = { "extra_arguments", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_preserve_path_from] = { "preserve_path_from", obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	make_obj(wk, res, obj_generated_list);
	struct obj_generated_list *gl = get_obj_generated_list(wk, *res);
	gl->generator = gen;
	gl->extra_arguments = akw[kw_extra_arguments].val;
	gl->preserve_path_from = akw[kw_preserve_path_from].val;

	if (!coerce_files(wk, an[0].node, an[0].val, &gl->input)) {
		return false;
	}

	return true;
}

const struct func_impl_name impl_tbl_generator[] = {
	{ "process", func_generator_process },
	{ NULL, NULL },
};
