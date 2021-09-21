#include "posix.h"

#include "backend/ninja.h"
#include "external/samu.h"
#include "functions/default/setup.h"
#include "lang/eval.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

uint32_t func_setup_flags = 0;

struct set_options_ctx {
	struct workspace *sub_wk;
	uint32_t err_node, parent;
};

static enum iteration_result
set_options_iter(struct workspace *wk, void *_ctx, uint32_t key, uint32_t val)
{
	struct set_options_ctx *ctx = _ctx;

	switch (get_obj(wk, val)->type) {
	case obj_dict:
		if (ctx->parent) {
			interp_error(wk, ctx->err_node, "options nested too deep");
			return ir_err;
		}

		ctx->parent = key;

		if (!obj_dict_foreach(wk, val, ctx, set_options_iter)) {
			return ir_err;
		}

		ctx->parent = 0;
		break;
	default: {
		struct option_override oo = { .obj_value = true };
		if (ctx->parent) {
			oo.proj = wk_str_push(ctx->sub_wk, wk_objstr(wk, ctx->parent));
		}

		oo.name = wk_str_push(ctx->sub_wk, wk_objstr(wk, key));

		if (!obj_clone(wk, ctx->sub_wk, val, &oo.val)) {
			return ir_err;
		}

		darr_push(&ctx->sub_wk->option_overrides, &oo);
		break;
	}
	}

	return ir_cont;
}

bool
func_setup(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_options,
		kw_source,
		kw_cross,
	};
	struct args_kw akw[] = {
		[kw_source] = { "source", obj_string },
		[kw_options] = { "options", obj_dict },
		[kw_cross] = { "cross", obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	struct workspace sub_wk;
	bool ret = false;
	char build_ninja[PATH_MAX];
	uint32_t project_id;

	if (akw[kw_source].set) {
		L("chdir to '%s'", wk_objstr(wk, akw[kw_source].val));
		if (!path_chdir(wk_objstr(wk, akw[kw_source].val))) {
			return false;
		}
	}

	workspace_init(&sub_wk);

	if (!workspace_setup_dirs(&sub_wk, wk_objstr(wk, an[0].val), wk->argv0, true)) {
		goto ret;
	}

	if (akw[kw_options].set) {
		struct set_options_ctx ctx = {
			.sub_wk = &sub_wk,
			.err_node = akw[kw_options].node
		};

		if (!obj_dict_foreach(wk, akw[kw_options].val, &ctx, set_options_iter)) {
			goto ret;
		}
	}

	if (!path_join(build_ninja, PATH_MAX, sub_wk.build_root, "build.ninja")) {
		goto ret;
	}

	if ((func_setup_flags & func_setup_flag_force) || !fs_file_exists(build_ninja)) {
		if (!eval_project(&sub_wk, NULL, sub_wk.source_root, sub_wk.build_root, &project_id)) {
			goto ret;
		}

		if (!ninja_write_all(&sub_wk)) {
			goto ret;
		}
	}

	if (!(func_setup_flags & func_setup_flag_no_build) && have_samu) {
		if (!path_chdir(sub_wk.build_root)) {
			return false;
		} else if (!muon_samu(0, (char *[]){ "<muon_samu>", NULL })) {
			return false;
		} else if (!path_chdir(sub_wk.source_root)) {
			return false;
		}
	}

	ret = true;
ret:
	workspace_destroy(&sub_wk);

	if (akw[kw_source].set) {
		if (!path_chdir(wk->source_root)) {
			return false;
		}
	}
	return ret;
}
