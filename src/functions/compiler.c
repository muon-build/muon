#include "posix.h"

#include <limits.h>
#include <string.h>

#include "coerce.h"
#include "filesystem.h"
#include "functions/common.h"
#include "functions/compiler.h"
#include "interpreter.h"
#include "log.h"
#include "output.h"
#include "path.h"
#include "run_cmd.h"

struct func_compiler_get_supported_arguments_iter_ctx {
	uint32_t arr, node, compiler;
	char *test_c, *test_o;
};

static enum iteration_result
func_compiler_get_supported_arguments_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct func_compiler_get_supported_arguments_iter_ctx *ctx = _ctx;

	if (!typecheck(wk, ctx->node, val_id, obj_string)) {
		return ir_err;
	}

	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd(&cmd_ctx, "cc", (char *[]){
		"cc",
		"-Werror",
		wk_objstr(wk, val_id),
		"-x", "c",
		"-o", ctx->test_o,
		"-c",
		ctx->test_c,
		NULL,
	})) {
		if (cmd_ctx.err_msg) {
			interp_error(wk, ctx->node, "error: %s", cmd_ctx.err_msg);
		} else {
			interp_error(wk, ctx->node, "error: %s", strerror(cmd_ctx.err_no));
		}
		return ir_err;
	}

	LOG_I("'%s' supported: %s",
		wk_objstr(wk, val_id),
		cmd_ctx.status == 0 ? "YES" : "NO"
		);

	if (cmd_ctx.status == 0) {
		obj_array_push(wk, ctx->arr, val_id);
	}


	return ir_cont;
}

static bool
func_compiler_get_supported_arguments(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_array);

	char test_c[PATH_MAX], test_o[PATH_MAX];
	output_private_file(wk, test_c, "test.c", "int main(void){}\n");

	strcpy(test_o, test_c);
	if (!path_add_suffix(test_o, PATH_MAX, ".o")) {
		return false;
	}

	return obj_array_foreach(wk, an[0].val,
		&(struct func_compiler_get_supported_arguments_iter_ctx) {
		.compiler = rcvr,
		.test_c = test_c,
		.test_o = test_o,
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

static bool
func_compiler_find_library(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_static,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required" },
		[kw_static] = { "static", obj_bool },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum requirement_type requirement;
	if (!coerce_requirement(wk, &akw[kw_required], &requirement)) {
		return false;
	}

	if (requirement == requirement_skip) {
		make_obj(wk, obj, obj_external_library)->dat.external_library.found = false;
		return true;
	}

	bool found = false;
	char buf[PATH_MAX + 1] = { 0 };
	const char *pre = "/usr/lib/lib", *suf[] = { "so", "a", NULL };
	uint32_t i;

	for (i = 0; suf[i]; ++i) {
		snprintf(buf, PATH_MAX, "%s%s.%s", pre, wk_objstr(wk, an[0].val), suf[i]);
		if (fs_file_exists(buf)) {
			found = true;
			break;
		}
	}

	if (!found) {
		if (requirement == requirement_required) {
			interp_error(wk, an[0].node, "library not found");
			return false;
		}

		make_obj(wk, obj, obj_external_library)->dat.external_library.found = false;
	} else {
		struct obj *external_library = make_obj(wk, obj, obj_external_library);
		external_library->dat.external_library.found = true;
		external_library->dat.external_library.full_path = wk_str_push(wk, buf);
	}


	return true;
}

const struct func_impl_name impl_tbl_compiler[] = {
	{ "get_supported_arguments", func_compiler_get_supported_arguments },
	{ "get_id", func_compiler_get_id },
	{ "find_library", func_compiler_find_library },
	{ NULL, NULL },
};
