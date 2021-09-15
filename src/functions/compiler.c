#include "posix.h"

#include <limits.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "coerce.h"
#include "compilers.h"
#include "functions/common.h"
#include "functions/compiler.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/dirs.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

struct func_compiler_get_supported_arguments_iter_ctx {
	uint32_t arr, node, compiler;
};

static bool
output_test_source(struct workspace *wk, enum compiler_language l, const char **source_file, const char **out_file)
{
	static bool already_output[compiler_language_count];
	static char test_source_path[PATH_MAX], test_out_path[PATH_MAX];
	const char *test_name = NULL, *test_source = NULL;

	switch (l) {
	case compiler_language_c_hdr:
	case compiler_language_cpp_hdr:
	case compiler_language_count:
		LOG_E("no test source for language: %s", compiler_language_to_s(l));
		return false;
	case compiler_language_c:
		test_name = "test.c";
		test_source = "int main(void){}\n";
		break;
	case compiler_language_cpp:
		test_name = "test.cpp";
		test_source = "int main(void){}\n";
		break;
	}

	assert(test_name && test_source);

	if (!path_join(test_source_path, PATH_MAX, wk->muon_private, test_name)) {
		return false;
	}

	strcpy(test_out_path, test_source_path);
	if (!path_add_suffix(test_out_path, PATH_MAX, ".o")) {
		return false;
	}

	*source_file = test_source_path;
	*out_file = test_out_path;

	if (already_output[l]) {
		return true;
	}
	already_output[l] = true;

	return fs_write(test_source_path, (uint8_t *)test_source, strlen(test_source));
}

static bool
compiler_has_argument(struct workspace *wk, uint32_t comp_id, uint32_t err_node, uint32_t arg, bool *has_argument)
{
	struct obj *comp = get_obj(wk, comp_id);

	const char *test_source, *test_out;
	if (!output_test_source(wk, comp->dat.compiler.lang, &test_source, &test_out)) {
		return ir_err;
	}

	if (!typecheck(wk, err_node, arg, obj_string)) {
		return ir_err;
	}

	char *name = wk_str(wk, comp->dat.compiler.name);
	enum compiler_type t = comp->dat.compiler.type;

	const char *argv[MAX_ARGS + 1] = { name };
	uint32_t len = 1;
	push_argv(argv, &len, MAX_ARGS, compilers[t].args.werror());
	push_argv_single(argv, &len, MAX_ARGS, wk_objstr(wk, arg));
	push_argv(argv, &len, MAX_ARGS, compilers[t].args.compile_only());
	push_argv(argv, &len, MAX_ARGS, compilers[t].args.output(test_out));
	push_argv_single(argv, &len, MAX_ARGS, test_source);

	bool ret = false;
	struct run_cmd_ctx cmd_ctx = { 0 };

	if (!run_cmd(&cmd_ctx, name, (char * const *)argv, NULL)) {
		interp_error(wk, err_node, "error: %s", cmd_ctx.err_msg);
		goto ret;
	}

	*has_argument = cmd_ctx.status == 0;

	LOG_I("'%s' supported: %s",
		wk_objstr(wk, arg),
		*has_argument ? "\033[32mYES\033[0m" : "\033[31mNO\033[0m"
		);

	ret = true;
ret:
	run_cmd_ctx_destroy(&cmd_ctx);
	return ret;
}

static enum iteration_result
func_compiler_get_supported_arguments_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct func_compiler_get_supported_arguments_iter_ctx *ctx = _ctx;
	bool has_argument;

	if (!compiler_has_argument(wk, ctx->compiler, ctx->node, val_id, &has_argument)) {
		return false;
	}

	if (has_argument) {
		obj_array_push(wk, ctx->arr, val_id);
	}

	return ir_cont;
}

static bool
func_compiler_has_argument(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	bool has_argument;
	if (!compiler_has_argument(wk, rcvr, an[0].node, an[0].val, &has_argument)) {
		return false;
	}

	make_obj(wk, obj, obj_bool)->dat.boolean = has_argument;
	return true;
}

static bool
func_compiler_get_supported_arguments(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_array);

	return obj_array_foreach(wk, an[0].val,
		&(struct func_compiler_get_supported_arguments_iter_ctx) {
		.compiler = rcvr,
		.arr = *obj,
		.node = an[0].node,
	}, func_compiler_get_supported_arguments_iter);
}

static bool
func_compiler_get_id(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, compiler_type_to_s(get_obj(wk, rcvr)->dat.compiler.type));
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
		[kw_required] = { "required", obj_any },
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
	char lib[PATH_MAX], path[PATH_MAX];
	const char *suf[] = { "so", "a", NULL };

	uint32_t i, j;
	for (i = 0; libdirs[i]; ++i) {
		for (j = 0; suf[j]; ++j) {
			snprintf(lib, PATH_MAX, "lib%s.%s", wk_objstr(wk, an[0].val), suf[j]);

			if (!path_join(path, PATH_MAX, libdirs[i], lib)) {
				return false;
			}

			if (fs_file_exists(path)) {
				found = true;
				goto done;
			}
		}
	}

done:
	if (!found) {
		if (requirement == requirement_required) {
			interp_error(wk, an[0].node, "library not found");
			return false;
		}

		make_obj(wk, obj, obj_external_library)->dat.external_library.found = false;
	} else {
		LOG_I("found library '%s' at '%s'", wk_objstr(wk, an[0].val), path);
		struct obj *external_library = make_obj(wk, obj, obj_external_library);
		external_library->dat.external_library.found = true;
		external_library->dat.external_library.full_path = wk_str_push(wk, path);
	}

	return true;
}

static bool
func_compiler_cmd_array(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_array);
	obj s;
	make_obj(wk, &s, obj_string)->dat.str = get_obj(wk, rcvr)->dat.compiler.name;
	obj_array_push(wk, *res, s);
	return true;
}


const struct func_impl_name impl_tbl_compiler[] = {
	{ "get_supported_arguments", func_compiler_get_supported_arguments },
	{ "has_argument", func_compiler_has_argument },
	{ "get_id", func_compiler_get_id },
	{ "find_library", func_compiler_find_library },
	{ "cmd_array", func_compiler_cmd_array },
	{ NULL, NULL },
};
