#include "posix.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "coerce.h"
#include "compilers.h"
#include "error.h"
#include "functions/common.h"
#include "functions/compiler.h"
#include "functions/kernel/dependency.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

static const char *
bool_to_yn(bool v)
{
	return v ? "\033[32mYES\033[0m" : "\033[31mNO\033[0m";
}

static bool
write_test_source(struct workspace *wk, const struct str *src, enum compiler_language l, const char **res)
{
	static char test_source_path[PATH_MAX];
	if (!path_join(test_source_path, PATH_MAX, wk->muon_private, "test.")) {
		return false;
	} else if (!path_add_suffix(test_source_path, PATH_MAX, compiler_language_extension(l))) {
		return false;
	}

	*res = test_source_path;

	if (!fs_write(test_source_path, (const uint8_t *)src->s, src->len)) {
		return false;
	}

	return true;
}

enum compile_mode {
	compile_mode_preprocess,
	compile_mode_compile,
	compile_mode_link,
	compile_mode_run,
};

struct compiler_check_opts {
	struct run_cmd_ctx cmd_ctx;
	enum compile_mode mode;
	obj comp_id;
	struct args_kw *deps, *inc, *required;
	obj args;
	bool skip_run_check;
	bool src_is_path;
	const char *output_path;
};

static void
add_extra_compiler_check_args(struct workspace *wk, struct obj_compiler *comp, obj args)
{
	if (comp->lang == compiler_language_cpp) {
		// From meson:
		// -fpermissive allows non-conforming code to compile which is necessary
		// for many C++ checks. Particularly, the has_header_symbol check is
		// too strict without this and always fails.
		obj_array_push(wk, args, make_str(wk, "-fpermissive"));
	}
}

static bool
compiler_check(struct workspace *wk, struct compiler_check_opts *opts,
	const char *src, uint32_t err_node, bool *res)
{
	enum requirement_type req = requirement_auto;
	if (opts->required && opts->required->set) {
		if (!coerce_requirement(wk, opts->required, &req)) {
			return false;
		}
	}

	if (req == requirement_skip) {
		*res = false;
		return true;
	}

	struct obj_compiler *comp = get_obj_compiler(wk, opts->comp_id);
	enum compiler_type t = comp->type;

	obj compiler_args;
	make_obj(wk, &compiler_args, obj_array);

	obj_array_push(wk, compiler_args, comp->name);

	get_std_args(wk, current_project(wk), NULL, compiler_args, comp->lang, t);

	add_extra_compiler_check_args(wk, comp, compiler_args);

	switch (opts->mode) {
	case compile_mode_run:
	case compile_mode_link:
		get_option_link_args(wk, current_project(wk), NULL, compiler_args, comp->lang);
	/* fallthrough */
	case compile_mode_compile:
		get_option_compile_args(wk, current_project(wk), NULL, compiler_args, comp->lang);
	/* fallthrough */
	case compile_mode_preprocess:
		break;
	}

	if (opts->inc && opts->inc->set) {
		obj include_dirs = 0;
		if (!coerce_include_dirs(wk, opts->inc->node, opts->inc->val, false, &include_dirs)) {
			return false;
		}

		struct setup_compiler_args_includes_ctx inc_ctx = {
			.args = compiler_args,
			.t = t,
			.dont_relativize = true
		};

		if (!obj_array_foreach(wk, include_dirs, &inc_ctx, setup_compiler_args_includes)) {
			return false;
		}
	}

	switch (opts->mode) {
	case compile_mode_preprocess:
		push_args(wk, compiler_args, compilers[t].args.preprocess_only());
		break;
	case compile_mode_compile:
		push_args(wk, compiler_args, compilers[t].args.compile_only());
		break;
	case compile_mode_run:
		break;
	case compile_mode_link:
		push_args(wk, compiler_args, linkers[compilers[t].linker].args.fatal_warnings());
		break;
	}

	const char *path;
	if (opts->src_is_path) {
		path = src;
	} else {
		if (!write_test_source(wk, &WKSTR(src), comp->lang, &path)) {
			return false;
		}
	}

	obj_array_push(wk, compiler_args, make_str(wk, path));

	const char *output = "/dev/null";
	if (opts->output_path) {
		output = opts->output_path;;
	} else if (opts->mode == compile_mode_run) {
		static char test_output_path[PATH_MAX];
		if (!path_join(test_output_path, PATH_MAX, wk->muon_private, "compiler_check_exe")) {
			return false;
		}
		output = test_output_path;
	}

	push_args(wk, compiler_args, compilers[t].args.output(output));

	if (opts->deps && opts->deps->set) {
		struct build_dep dep = { 0 };
		dep_process_deps(wk, opts->deps->val, &dep);

		struct setup_linker_args_ctx sctx = {
			.linker = compilers[t].linker,
			.link_lang = comp->lang,
			.args = &dep
		};

		setup_linker_args(wk, NULL, NULL, &sctx);
		obj_array_extend_nodup(wk, compiler_args, dep.link_args);
	}

	if (opts->args) {
		obj_array_extend(wk, compiler_args, opts->args);
	}

	bool ret = false;
	struct run_cmd_ctx cmd_ctx = { 0 };

	const char *argstr;
	join_args_argstr(wk, &argstr, compiler_args);

	L("compiling: '%s'", path);

	if (!run_cmd(&cmd_ctx, argstr, NULL)) {
		interp_error(wk, err_node, "error: %s", cmd_ctx.err_msg);
		goto ret;
	}

	L("compiler stdout: '%s'", cmd_ctx.err.buf);
	L("compiler stderr: '%s'", cmd_ctx.out.buf);

	if (opts->mode == compile_mode_run) {
		if (cmd_ctx.status != 0) {
			if (opts->skip_run_check) {
				*res = false;
				ret = true;
				goto ret;
			} else {
				LOG_W("failed to compile test, rerun with -v to see compiler invocation");
				goto ret;
			}
		}

		if (!run_cmd_argv(&opts->cmd_ctx, output, (char *const []){ (char *)output, NULL }, NULL)) {
			LOG_W("compiled binary failed to run: %s", opts->cmd_ctx.err_msg);
			run_cmd_ctx_destroy(&opts->cmd_ctx);
			goto ret;
		} else if (!opts->skip_run_check && opts->cmd_ctx.status != 0) {
			LOG_W("compiled binary returned an error (exit code %d)", opts->cmd_ctx.status);
			run_cmd_ctx_destroy(&opts->cmd_ctx);
			goto ret;
		}

		*res = true;
	} else {
		*res = cmd_ctx.status == 0;
	}

	ret = true;
ret:
	run_cmd_ctx_destroy(&cmd_ctx);
	if (!*res && req == requirement_required) {
		assert(opts->required);
		interp_error(wk, opts->required->node, "a required compiler check failed");
		return false;
	}
	return ret;
}

static int64_t
compiler_check_parse_output_int(struct compiler_check_opts *opts)
{
	char *endptr;
	int64_t size;
	size = strtol(opts->cmd_ctx.out.buf, &endptr, 10);
	if (*endptr) {
		LOG_W("compiler check binary had malformed output '%s'", opts->cmd_ctx.out.buf);
		return -1;
	}

	return size;
}

enum cc_kwargs {
	cc_kw_args,
	cc_kw_dependencies,
	cc_kw_prefix,
	cc_kw_required,
	cc_kw_include_directories,
	cc_kw_name,
	cc_kw_guess,
	cc_kw_high,
	cc_kw_low,
	cc_kwargs_count,

	cm_kw_args = 1 << 0,
	cm_kw_dependencies = 1 << 1,
	cm_kw_prefix = 1 << 2,
	cm_kw_required = 1 << 3,
	cm_kw_include_directories = 1 << 4,
	cm_kw_name = 1 << 5,

	cm_kw_guess = 1 << 6,
	cm_kw_high = 1 << 7,
	cm_kw_low = 1 << 8,
};

static bool
func_compiler_check_args_common(struct workspace *wk, obj rcvr, uint32_t args_node,
	struct args_norm *an, struct args_kw **kw_res, struct compiler_check_opts *opts,
	enum cc_kwargs args_mask)
{
	static struct args_kw akw[cc_kwargs_count + 1] = { 0 };
	struct args_kw akw_base[] = {
		[cc_kw_args] = { "args", ARG_TYPE_ARRAY_OF | obj_string },
		[cc_kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | tc_dependency },
		[cc_kw_prefix] = { "prefix", obj_string },
		[cc_kw_required] = { "required", tc_required_kw },
		[cc_kw_include_directories] = { "include_directories", ARG_TYPE_ARRAY_OF | tc_coercible_inc },
		[cc_kw_name] = { "name", obj_string },
		[cc_kw_guess] = { "guess", obj_number, },
		[cc_kw_high] = { "high", obj_number, },
		[cc_kw_low] = { "low", obj_number, },
		0
	};

	memcpy(akw, akw_base, sizeof(struct args_kw) * cc_kwargs_count);

	struct args_kw *use_akw;
	if (kw_res && args_mask) {
		*kw_res = akw;
		use_akw = akw;
	} else {
		use_akw = NULL;
	}

	if (!interp_args(wk, args_node, an, NULL, use_akw)) {
		return false;
	}

	if (use_akw) {
		uint32_t i;
		for (i = 0; i < cc_kwargs_count; ++i) {
			if ((args_mask & (1 << i))) {
				continue;
			} else if (akw[i].set) {
				interp_error(wk, akw[i].node, "invalid keyword '%s'", akw[i].key);
				return false;
			}
		}
	}

	opts->comp_id = rcvr;
	if (akw[cc_kw_dependencies].set) {
		opts->deps = &akw[cc_kw_dependencies];
	}

	if (akw[cc_kw_args].set) {
		opts->args = akw[cc_kw_args].val;
	}

	if (akw[cc_kw_include_directories].set) {
		opts->inc = &akw[cc_kw_include_directories];
	}

	if (akw[cc_kw_required].set) {
		opts->required = &akw[cc_kw_required];
	}
	return true;
}

static const char *
compiler_check_prefix(struct workspace *wk, struct args_kw *akw)
{
	if (akw[cc_kw_prefix].set) {
		return get_cstr(wk, akw[cc_kw_prefix].val);
	} else {
		return "";
	}
}

static bool
func_compiler_sizeof(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compile_mode_run,
		.skip_run_check = true,
	};

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix)) {
		return false;
	}

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"#include <stdio.h>\n"
		"%s\n"
		"int main(void) { printf(\"%%ld\", (long)(sizeof(%s))); return 0; }\n",
		compiler_check_prefix(wk, akw),
		get_cstr(wk, an[0].val)
		);

	bool ok;
	int64_t size;
	if (compiler_check(wk, &opts, src, an[0].node, &ok) && ok) {
		size = compiler_check_parse_output_int(&opts);
	} else {
		size  = -1;
	}

	make_obj(wk, res, obj_number);
	set_obj_number(wk, *res, size);
	run_cmd_ctx_destroy(&opts.cmd_ctx);

	LOG_I("sizeof %s: %" PRId64,
		get_cstr(wk, an[0].val),
		get_obj_number(wk, *res)
		);

	return true;
}

static bool
func_compiler_alignment(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compile_mode_run,
	};

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix)) {
		return false;
	}

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"#include <stdio.h>\n"
		"#include <stddef.h>\n"
		"%s\n"
		"struct tmp { char c; %s target; };\n"
		"int main(void) { printf(\"%%d\", (int)(offsetof(struct tmp, target))); return 0; }\n",
		compiler_check_prefix(wk, akw),
		get_cstr(wk, an[0].val)
		);

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok) || !ok) {
		return false;
	}

	make_obj(wk, res, obj_number);
	set_obj_number(wk, *res, compiler_check_parse_output_int(&opts));
	run_cmd_ctx_destroy(&opts.cmd_ctx);

	LOG_I("alignment of %s: %" PRId64,
		get_cstr(wk, an[0].val),
		get_obj_number(wk, *res)
		);

	return true;
}

static bool
func_compiler_compute_int(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compile_mode_run,
	};

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_include_directories
		| cm_kw_guess | cm_kw_high | cm_kw_low)) {
		return false;
	}

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"#include <stdio.h>\n"
		"%s\n"
		"int main(void) {\n"
		"int d = (%s);\n"
		"printf(\"%%d\", d);\n"
		"}\n",
		compiler_check_prefix(wk, akw),
		get_cstr(wk, an[0].val)
		);

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok) || !ok) {
		return false;
	}

	make_obj(wk, res, obj_number);
	set_obj_number(wk, *res, compiler_check_parse_output_int(&opts));
	run_cmd_ctx_destroy(&opts.cmd_ctx);
	return true;
}

static bool
get_has_function_attribute_test(const struct str *name, const char **res)
{
	/* These functions are based on the following code:
	 * https://git.savannah.gnu.org/gitweb/?p=autoconf-archive.git;a=blob_plain;f=m4/ax_gcc_func_attribute.m4,
	 * which is licensed under the following terms:
	 *
	 *   Copyright (c) 2013 Gabriele Svelto <gabriele.svelto@gmail.com>
	 *
	 *   Copying and distribution of this file, with or without modification, are
	 *   permitted in any medium without royalty provided the copyright notice
	 *   and this notice are preserved.  This file is offered as-is, without any
	 *   warranty.
	 */
	struct { const char *name, *src; } tests[] = {
		{ "alias",
		  "#ifdef __cplusplus\n"
		  "extern \"C\" {\n"
		  "#endif\n"
		  "int foo(void) { return 0; }\n"
		  "int bar(void) __attribute__((alias(\"foo\")));\n"
		  "#ifdef __cplusplus\n"
		  "}\n"
		  "#endif\n" },
		{ "aligned",
		  "int foo(void) __attribute__((aligned(32)));\n" },
		{ "alloc_size",
		  "void *foo(int a) __attribute__((alloc_size(1)));\n" },
		{ "always_inline",
		  "inline __attribute__((always_inline)) int foo(void) { return 0; }\n" },
		{ "artificial",
		  "inline __attribute__((artificial)) int foo(void) { return 0; }\n" },
		{ "cold",
		  "int foo(void) __attribute__((cold));\n" },
		{ "const",
		  "int foo(void) __attribute__((const));\n" },
		{ "constructor",
		  "int foo(void) __attribute__((constructor));\n" },
		{ "constructor_priority",
		  "int foo( void ) __attribute__((__constructor__(65535/2)));\n" },
		{ "deprecated",
		  "int foo(void) __attribute__((deprecated(\"\")));\n" },
		{ "destructor",
		  "int foo(void) __attribute__((destructor));\n" },
		{ "dllexport",
		  "__declspec(dllexport) int foo(void) { return 0; }\n" },
		{ "dllimport",
		  "__declspec(dllimport) int foo(void);\n" },
		{ "error",
		  "int foo(void) __attribute__((error(\"\")));\n" },
		{ "externally_visible",
		  "int foo(void) __attribute__((externally_visible));\n" },
		{ "fallthrough",
		  "int foo( void ) {\n"
		  "  switch (0) {\n"
		  "    case 1: __attribute__((fallthrough));\n"
		  "    case 2: break;\n"
		  "  }\n"
		  "  return 0;\n"
		  "};\n" },
		{ "flatten",
		  "int foo(void) __attribute__((flatten));\n" },
		{ "format",
		  "int foo(const char * p, ...) __attribute__((format(printf, 1, 2)));\n" },
		{ "format_arg",
		  "char * foo(const char * p) __attribute__((format_arg(1)));\n" },
		{ "force_align_arg_pointer",
		  "__attribute__((force_align_arg_pointer)) int foo(void) { return 0; }\n" },
		{ "gnu_inline",
		  "inline __attribute__((gnu_inline)) int foo(void) { return 0; }\n" },
		{ "hot",
		  "int foo(void) __attribute__((hot));\n" },
		{ "ifunc",
		  "('int my_foo(void) { return 0; }'\n"
		  " static int (*resolve_foo(void))(void) { return my_foo; }'\n"
		  " int foo(void) __attribute__((ifunc(\"resolve_foo\")));'),\n" },
		{ "leaf",
		  "__attribute__((leaf)) int foo(void) { return 0; }\n" },
		{ "malloc",
		  "int *foo(void) __attribute__((malloc));\n" },
		{ "noclone",
		  "int foo(void) __attribute__((noclone));\n" },
		{ "noinline",
		  "__attribute__((noinline)) int foo(void) { return 0; }\n" },
		{ "nonnull",
		  "int foo(char * p) __attribute__((nonnull(1)));\n" },
		{ "noreturn",
		  "int foo(void) __attribute__((noreturn));\n" },
		{ "nothrow",
		  "int foo(void) __attribute__((nothrow));\n" },
		{ "optimize",
		  "__attribute__((optimize(3))) int foo(void) { return 0; }\n" },
		{ "packed",
		  "struct __attribute__((packed)) foo { int bar; };\n" },
		{ "pure",
		  "int foo(void) __attribute__((pure));\n" },
		{ "returns_nonnull",
		  "int *foo(void) __attribute__((returns_nonnull));\n" },
		{ "unused",
		  "int foo(void) __attribute__((unused));\n" },
		{ "used",
		  "int foo(void) __attribute__((used));\n" },
		{ "visibility",
		  "int foo_def(void) __attribute__((visibility(\"default\")));\n"
		  "int foo_hid(void) __attribute__((visibility(\"hidden\")));\n"
		  "int foo_int(void) __attribute__((visibility(\"internal\")));\n" },
		{ "visibility:default",
		  "int foo(void) __attribute__((visibility(\"default\")));\n" },
		{ "visibility:hidden",
		  "int foo(void) __attribute__((visibility(\"hidden\")));\n" },
		{ "visibility:internal",
		  "int foo(void) __attribute__((visibility(\"internal\")));\n" },
		{ "visibility:protected",
		  "int foo(void) __attribute__((visibility(\"protected\")));\n" },
		{ "warning",
		  "int foo(void) __attribute__((warning(\"\")));\n" },
		{ "warn_unused_result",
		  "int foo(void) __attribute__((warn_unused_result));\n" },
		{ "weak",
		  "int foo(void) __attribute__((weak));\n" },
		{ "weakref",
		  "static int foo(void) { return 0; }\n"
		  "static int var(void) __attribute__((weakref(\"foo\")));\n" },
		{ 0 }
	};

	uint32_t i;
	for (i = 0; tests[i].name; ++i) {
		if (str_eql(name, &WKSTR(tests[i].name))) {
			*res = tests[i].src;
			return true;
		}
	}

	return false;
}

static bool
compiler_has_function_attribute(struct workspace *wk, obj comp_id, uint32_t err_node, obj arg, bool *has_fattr)
{
	struct compiler_check_opts opts = {
		.mode = compile_mode_compile,
		.comp_id = comp_id,
	};

	const char *src;
	if (!get_has_function_attribute_test(get_str(wk, arg), &src)) {
		interp_error(wk, err_node, "unknown attribute '%s'", get_cstr(wk, arg));
		return false;
	}

	if (!compiler_check(wk, &opts, src, err_node, has_fattr)) {
		return false;
	}

	LOG_I("have attribute %s: %s",
		get_cstr(wk, arg),
		bool_to_yn(*has_fattr)
		);

	return true;
}

static bool
func_compiler_has_function_attribute(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	bool has_fattr;
	if (!compiler_has_function_attribute(wk, rcvr, an[0].node, an[0].val, &has_fattr)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, has_fattr);
	return true;
}

struct func_compiler_get_supported_function_attributes_iter_ctx {
	uint32_t node;
	obj arr, compiler;
};

static enum iteration_result
func_compiler_get_supported_function_attributes_iter(struct workspace *wk, void *_ctx, obj val_id)
{
	struct func_compiler_get_supported_function_attributes_iter_ctx *ctx = _ctx;
	bool has_fattr;

	if (!compiler_has_function_attribute(wk, ctx->compiler, ctx->node, val_id, &has_fattr)) {
		return ir_err;
	}

	if (has_fattr) {
		obj_array_push(wk, ctx->arr, val_id);
	}

	return ir_cont;
}

static bool
func_compiler_get_supported_function_attributes(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB | obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_array);

	return obj_array_foreach_flat(wk, an[0].val,
		&(struct func_compiler_get_supported_function_attributes_iter_ctx) {
		.compiler = rcvr,
		.arr = *res,
		.node = an[0].node,
	}, func_compiler_get_supported_function_attributes_iter);
}

static bool
func_compiler_has_function(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compile_mode_link,
	};

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix)) {
		return false;
	}

	const char *prefix = compiler_check_prefix(wk, akw),
		   *func = get_cstr(wk, an[0].val);

	bool prefix_contains_include = strstr(prefix, "#include") != NULL;

	char src[BUF_SIZE_4k];
	if (prefix_contains_include) {
		snprintf(src, BUF_SIZE_4k,
			"%s\n"
			"#include <limits.h>\n"
			"#if defined __stub_%s || defined __stub___%s\n"
			"fail fail fail this function is not going to work\n"
			"#endif\n"
			"int main(void) {\n"
			"void *a = (void*) &%s;\n"
			"long long b = (long long) a;\n"
			"return (int) b;\n"
			"}\n",
			prefix,
			func, func,
			func
			);
	} else {
		snprintf(src, BUF_SIZE_4k,
			"#define %s muon_disable_define_of_%s\n"
			"%s\n"
			"#include <limits.h>\n"
			"#undef %s\n"
			"#ifdef __cplusplus\n"
			"extern \"C\"\n"
			"#endif\n"
			"char %s (void);\n"
			"#if defined __stub_%s || defined __stub___%s\n"
			"fail fail fail this function is not going to work\n"
			"#endif\n"
			"int main(void) { return %s(); }\n",
			func, func,
			prefix,
			func,
			func,
			func, func,
			func
			);
	}

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
		return false;
	}

	if (!ok) {
		bool is_builtin = str_startswith(get_str(wk, an[0].val), &WKSTR("__builtin_"));
		const char *__builtin_ = is_builtin ? "" : "__builtin_";

		/* With some toolchains (MSYS2/mingw for example) the compiler
		 * provides various builtins which are not really implemented and
		 * fall back to the stdlib where they aren't provided and fail at
		 * build/link time. In case the user provides a header, including
		 * the header didn't lead to the function being defined, and the
		 * function we are checking isn't a builtin itself we assume the
		 * builtin is not functional and we just error out. */
		snprintf(src, BUF_SIZE_4k,
			"%s\n"
			"int main(void) {\n"
			"#if !%d && !defined(%s) && !%d\n"
			"	#error \"No definition for %s%s found in the prefix\"\n"
			"#endif\n"
			"#ifdef __has_builtin\n"
			"	#if !__has_builtin(%s%s)\n"
			"		#error \"%s%s not found\"\n"
			"	#endif\n"
			"#elif ! defined(%s)\n"
			"	%s%s;\n"
			"#endif\n"
			"return 0;\n"
			"}\n",
			prefix,
			!prefix_contains_include, func, is_builtin,
			__builtin_, func,
			__builtin_, func,
			__builtin_, func,
			func,
			__builtin_, func
			);

		if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
			return false;
		}
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, ok);
	LOG_I("have function %s: %s",
		get_cstr(wk, an[0].val),
		bool_to_yn(ok)
		);

	return true;
}

static bool
compiler_has_header_symbol_c(struct workspace *wk, uint32_t node,
	struct compiler_check_opts *opts, const char *prefix,
	obj header, obj symbol, bool *res)
{
	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"%s\n"
		"#include <%s>\n"
		"int main(void) {\n"
		"    /* If it's not defined as a macro, try to use as a symbol */\n"
		"    #ifndef %s\n"
		"        %s;\n"
		"    #endif\n"
		"    return 0;\n"
		"}\n",
		prefix,
		get_cstr(wk, header),
		get_cstr(wk, symbol),
		get_cstr(wk, symbol)
		);

	if (!compiler_check(wk, opts, src, node, res)) {
		return false;
	}

	return true;
}

static bool
compiler_has_header_symbol_cpp(struct workspace *wk, uint32_t node,
	struct compiler_check_opts *opts, const char *prefix,
	obj header, obj symbol, bool *res)
{
	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"%s\n"
		"#include <%s>\n"
		"using %s;\n"
		"int main(void) {\n"
		"    return 0;\n"
		"}\n",
		prefix,
		get_cstr(wk, header),
		get_cstr(wk, symbol)
		);

	if (!compiler_check(wk, opts, src, node, res)) {
		return false;
	}

	return true;
}

static bool
func_compiler_has_header_symbol(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compile_mode_compile,
	};

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_required
		| cm_kw_include_directories)) {
		return false;
	}

	bool ok;
	switch (get_obj_compiler(wk, rcvr)->lang) {
	case compiler_language_c:
		if (!compiler_has_header_symbol_c(wk, an[0].node, &opts,
			compiler_check_prefix(wk, akw), an[0].val, an[1].val, &ok)) {
			return false;
		}
		break;
	case compiler_language_cpp:
		if (!compiler_has_header_symbol_c(wk, an[0].node, &opts,
			compiler_check_prefix(wk, akw), an[0].val, an[1].val, &ok)) {
			return false;
		}

		if (!ok) {
			if (!compiler_has_header_symbol_cpp(wk, an[0].node, &opts,
				compiler_check_prefix(wk, akw), an[0].val, an[1].val, &ok)) {
				return false;
			}
		}
		break;
	default:
		UNREACHABLE;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, ok);
	LOG_I("%s has header symbol %s: %s",
		get_cstr(wk, an[0].val),
		get_cstr(wk, an[1].val),
		bool_to_yn(ok)
		);

	return true;
}

static bool
compiler_get_define(struct workspace *wk, uint32_t err_node,
	struct compiler_check_opts *opts, const char *prefix, const char *def, obj *res)
{
	static char output_path[PATH_MAX];
	if (!path_join(output_path, PATH_MAX, wk->muon_private, "get_define_output")) {
		return false;
	}

	opts->output_path = output_path;
	opts->mode = compile_mode_preprocess;

	char src[BUF_SIZE_4k];
	const char *delim = "MUON_GET_DEFINE_DELIMITER\n";
	const uint32_t delim_len = strlen(delim);
	snprintf(src, BUF_SIZE_4k,
		"%s\n"
		"#ifndef %s\n"
		"#define %s\n"
		"#endif \n"
		"%s%s\n",
		prefix,
		def,
		def,
		delim,
		def
		);

	struct source output = { 0 };
	bool ok;
	if (!compiler_check(wk, opts, src, err_node, &ok)) {
		return false;
	} else if (!ok) {
		goto failed;
	}

	if (!fs_read_entire_file(output_path, &output)) {
		return false;
	}

	*res = make_str(wk, "");
	bool started = false;
	bool in_quotes = false;
	bool esc = false;
	bool joining = false;

	uint32_t i;
	for (i = 0; i < output.len; ++i) {
		if (!started && strncmp(&output.src[i], delim, delim_len) == 0) {
			i += delim_len;
			started = true;

			if (i >= output.len) {
				break;
			}
		}

		if (!started) {
			continue;
		}

		switch (output.src[i]) {
		case '"':
			if (esc) {
				esc = false;
			} else {
				in_quotes = !in_quotes;
				if (!in_quotes || joining) {
					uint32_t start = i;
					++i;
					for (; i < output.len; ++i) {
						if (!strchr("\t ", output.src[i])) {
							break;
						}
					}

					if (output.src[i] == '"') {
						joining = true;
						++i;
					} else {
						i = start;
					}
				}
			}
			break;
		case '\\':
			esc = true;
			break;
		}

		if (output.src[i] == '\n') {
			break;
		}

		if (started) {
			str_appn(wk, *res, &output.src[i], 1);
		}
	}

	LOG_I("got define %s", def);

	fs_source_destroy(&output);
	return true;
failed:
	fs_source_destroy(&output);
	interp_error(wk, err_node, "failed to get define: '%s'", def);
	return false;
}

static bool
func_compiler_get_define(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;

	struct compiler_check_opts opts = { 0 };

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix
		| cm_kw_include_directories)) {
		return false;
	}

	if (!compiler_get_define(wk, an[0].node, &opts, compiler_check_prefix(wk, akw), get_cstr(wk, an[0].val), res)) {
		return false;
	}
	return true;
}

static bool
func_compiler_symbols_have_underscore_prefix(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct compiler_check_opts opts = { .comp_id = rcvr };

	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	obj pre;
	if (!compiler_get_define(wk, args_node, &opts, "", "__USER_LABEL_PREFIX__", &pre)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, str_eql(get_str(wk, pre), &WKSTR("_")));
	return true;
}

static bool
func_compiler_check_common(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res, enum compile_mode mode)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = mode,
	};
	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_name
		| cm_kw_include_directories)) {
		return false;
	}

	enum obj_type t = get_obj_type(wk, an[0].val);

	const char *src;

	switch (t) {
	case obj_string:
		src = get_cstr(wk, an[0].val);
		break;
	case obj_file: {
		src  = get_file_path(wk, an[0].val);
		opts.src_is_path = true;
		break;
	}
	default:
		interp_error(wk, an[0].node, "expected file or string, got %s", obj_type_to_s(t));
		return false;
	}

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, ok);

	if (akw[cc_kw_name].set) {
		const char *mode_s = NULL;
		switch (mode) {
		case compile_mode_run:
			mode_s = "runs";
			break;
		case compile_mode_link:
			mode_s = "links";
			break;
		case compile_mode_compile:
			mode_s = "compiles";
			break;
		case compile_mode_preprocess:
			mode_s = "preprocesses";
			break;
		}

		LOG_I("%s %s: %s",
			get_cstr(wk, akw[cc_kw_name].val),
			mode_s,
			bool_to_yn(ok)
			);
	}

	return true;
}

static bool
func_compiler_compiles(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return func_compiler_check_common(wk, rcvr, args_node, res, compile_mode_compile);
}

static bool
func_compiler_links(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return func_compiler_check_common(wk, rcvr, args_node, res, compile_mode_link);
}

static bool
compiler_check_header(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res, enum compile_mode mode)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = mode,
	};

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_required
		| cm_kw_include_directories)) {
		return false;
	}

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"%s\n"
		"#include <%s>\n"
		"int main(void) {}\n",
		compiler_check_prefix(wk, akw),
		get_cstr(wk, an[0].val)
		);

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
		return false;
	}

	const char *mode_s = NULL;
	switch (mode) {
	case compile_mode_compile:
		mode_s = "usable";
		break;
	case compile_mode_preprocess:
		mode_s = "found";
		break;
	default:
		abort();
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, ok);
	LOG_I("header %s %s: %s",
		get_cstr(wk, an[0].val),
		mode_s,
		bool_to_yn(ok)
		);

	return true;
}

static bool
func_compiler_has_header(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_check_header(wk, rcvr, args_node, res, compile_mode_preprocess);
}

static bool
func_compiler_check_header(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_check_header(wk, rcvr, args_node, res, compile_mode_compile);
}

static bool
func_compiler_has_type(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compile_mode_compile,
	};

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix
		| cm_kw_include_directories)) {
		return false;
	}

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"%s\n"
		"void bar(void) { sizeof(%s); }\n",
		compiler_check_prefix(wk, akw),
		get_cstr(wk, an[0].val)
		);

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, ok);
	LOG_I("has type %s: %s",
		get_cstr(wk, an[0].val),
		bool_to_yn(ok)
		);

	return true;
}

static bool
compiler_has_member(struct workspace *wk, struct compiler_check_opts *opts,
	uint32_t err_node, const char *prefix, obj target, obj member, bool *res)
{
	opts->mode = compile_mode_compile;

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"%s\n"
		"void bar(void) {\n"
		"%s foo;\n"
		"foo.%s;\n"
		"}\n",
		prefix,
		get_cstr(wk, target),
		get_cstr(wk, member)
		);

	if (!compiler_check(wk, opts, src, err_node, res)) {
		return false;
	}

	LOG_I("%s has member %s: %s",
		get_cstr(wk, target),
		get_cstr(wk, member),
		bool_to_yn(*res)
		);

	return true;
}

static bool
func_compiler_has_member(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = { 0 };

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix
		| cm_kw_include_directories)) {
		return false;
	}

	bool ok;
	if (!compiler_has_member(wk, &opts, an[0].node,
		compiler_check_prefix(wk, akw), an[0].val, an[1].val, &ok)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, ok);
	return true;
}

struct compiler_has_members_ctx {
	struct compiler_check_opts *opts;
	uint32_t node;
	const char *prefix;
	obj target;
	bool ok;
};

static enum iteration_result
compiler_has_members_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct compiler_has_members_ctx *ctx = _ctx;

	if (!typecheck(wk, ctx->node, val, obj_string)) {
		return ir_err;
	}

	bool ok;
	if (!compiler_has_member(wk, ctx->opts, ctx->node,
		ctx->prefix, ctx->target, val, &ok)) {
		return ir_err;
	}

	if (!ok) {
		ctx->ok = false;
		return ir_done;
	}

	return ir_cont;
}

static bool
func_compiler_has_members(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { ARG_TYPE_GLOB | obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = { 0 };

	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_prefix
		| cm_kw_include_directories)) {
		return false;
	}

	if (!get_obj_array(wk, an[1].val)->len) {
		interp_error(wk, an[1].node, "missing member arguments");
		return false;
	}

	struct compiler_has_members_ctx ctx = {
		.opts = &opts,
		.node = an[0].node,
		.prefix = compiler_check_prefix(wk, akw),
		.target = an[0].val,
		.ok = true,
	};

	if (!obj_array_foreach_flat(wk, an[1].val, &ctx, compiler_has_members_iter)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, ctx.ok);
	return true;

}

static bool
func_compiler_run(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compile_mode_run,
		.skip_run_check = true,
	};
	if (!func_compiler_check_args_common(wk, rcvr, args_node, an, &akw, &opts,
		cm_kw_args | cm_kw_dependencies | cm_kw_name)) {
		return false;
	}

	obj o;
	if (!obj_array_flatten_one(wk, an[0].val, &o)) {
		interp_error(wk, an[0].node, "could not flatten argument");
	}

	enum obj_type t = get_obj_type(wk, an[0].val);

	const char *src;

	switch (t) {
	case obj_string:
		src = get_cstr(wk, an[0].val);
		break;
	case obj_file: {
		src  = get_file_path(wk, an[0].val);
		opts.src_is_path = true;
		break;
	}
	default:
		interp_error(wk, an[0].node, "expected file or string, got %s", obj_type_to_s(t));
		return false;
	}

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
		return false;
	}

	if (akw[cc_kw_name].set) {
		LOG_I("%s runs: %s",
			get_cstr(wk, akw[cc_kw_name].val),
			bool_to_yn(ok)
			);
	}

	make_obj(wk, res, obj_run_result);
	struct obj_run_result *rr = get_obj_run_result(wk, *res);
	rr->flags |= run_result_flag_from_compile;

	if (ok) {
		rr->flags |= run_result_flag_compile_ok;
		rr->out = make_strn(wk, opts.cmd_ctx.out.buf, opts.cmd_ctx.out.len);
		rr->err = make_strn(wk, opts.cmd_ctx.err.buf, opts.cmd_ctx.err.len);
		rr->status = opts.cmd_ctx.status;
	}

	run_cmd_ctx_destroy(&opts.cmd_ctx);
	return true;
}

static bool
compiler_has_argument(struct workspace *wk, obj comp_id, uint32_t err_node, obj arg, bool *has_argument, enum compile_mode mode)
{
	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	enum compiler_type t = comp->type;

	obj args;
	make_obj(wk, &args, obj_array);
	if (get_obj_type(wk, arg) == obj_string) {
		obj_array_push(wk, args, arg);
	} else {
		obj_array_extend(wk, args, arg);

		obj str;
		obj_array_join(wk, true, arg, make_str(wk, " "), &str);
		arg = str;
	}

	push_args(wk, args, compilers[t].args.werror());

	struct compiler_check_opts opts = {
		.mode = mode,
		.comp_id = comp_id,
		.args = args,
	};

	const char *src = "int main(void){}\n";
	if (!compiler_check(wk, &opts, src, err_node, has_argument)) {
		return false;
	}

	LOG_I("'%s' supported: %s",
		get_cstr(wk, arg),
		bool_to_yn(*has_argument)
		);

	return true;
}

struct func_compiler_get_supported_arguments_iter_ctx {
	uint32_t node;
	obj arr, compiler;
	enum compile_mode mode;
};

static enum iteration_result
func_compiler_get_supported_arguments_iter(struct workspace *wk, void *_ctx, obj val_id)
{
	struct func_compiler_get_supported_arguments_iter_ctx *ctx = _ctx;
	bool has_argument;

	if (!compiler_has_argument(wk, ctx->compiler, ctx->node, val_id, &has_argument, ctx->mode)) {
		return false;
	}

	if (has_argument) {
		obj_array_push(wk, ctx->arr, val_id);
	}

	return ir_cont;
}

static bool
compiler_has_argument_common(struct workspace *wk, obj rcvr, uint32_t args_node, uint32_t glob, obj *res, enum compile_mode mode)
{
	struct args_norm an[] = { { glob | obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	bool has_argument;
	if (!compiler_has_argument(wk, rcvr, an[0].node, an[0].val, &has_argument, mode)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, has_argument);
	return true;
}

static bool
func_compiler_has_argument(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_has_argument_common(wk, rcvr, args_node, 0, res, compile_mode_compile);
}

static bool
func_compiler_has_link_argument(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_has_argument_common(wk, rcvr, args_node, 0, res, compile_mode_link);
}

static bool
func_compiler_has_multi_arguments(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_has_argument_common(wk, rcvr, args_node, ARG_TYPE_GLOB, res, compile_mode_compile);
}

static bool
func_compiler_has_multi_link_arguments(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_has_argument_common(wk, rcvr, args_node, ARG_TYPE_GLOB, res, compile_mode_link);
}

static bool
compiler_get_supported_arguments(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res, enum compile_mode mode)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB | obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_array);

	return obj_array_foreach_flat(wk, an[0].val,
		&(struct func_compiler_get_supported_arguments_iter_ctx) {
		.compiler = rcvr,
		.arr = *res,
		.node = an[0].node,
		.mode = mode,
	}, func_compiler_get_supported_arguments_iter);
}

static bool
func_compiler_get_supported_arguments(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_get_supported_arguments(wk, rcvr, args_node, res, compile_mode_compile);
}

static bool
func_compiler_get_supported_link_arguments(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_get_supported_arguments(wk, rcvr, args_node, res, compile_mode_link);
}

static enum iteration_result
func_compiler_first_supported_argument_iter(struct workspace *wk, void *_ctx, obj val_id)
{
	struct func_compiler_get_supported_arguments_iter_ctx *ctx = _ctx;
	bool has_argument;

	if (!compiler_has_argument(wk, ctx->compiler, ctx->node, val_id, &has_argument, ctx->mode)) {
		return false;
	}

	if (has_argument) {
		LOG_I("first supported argument: '%s'", get_cstr(wk, val_id));
		obj_array_push(wk, ctx->arr, val_id);
		return ir_done;
	}

	return ir_cont;
}

static bool
compiler_first_supported_argument(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res, enum compile_mode mode)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB | obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_array);

	return obj_array_foreach_flat(wk, an[0].val,
		&(struct func_compiler_get_supported_arguments_iter_ctx) {
		.compiler = rcvr,
		.arr = *res,
		.node = an[0].node,
		.mode = mode,
	}, func_compiler_first_supported_argument_iter);
}

static bool
func_compiler_first_supported_argument(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_first_supported_argument(wk, rcvr, args_node, res, compile_mode_compile);
}

static bool
func_compiler_first_supported_link_argument(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return compiler_first_supported_argument(wk, rcvr, args_node, res, compile_mode_link);
}

static bool
func_compiler_get_id(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk, compiler_type_to_s(get_obj_compiler(wk, rcvr)->type));
	return true;
}

static bool
func_compiler_get_linker_id(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	enum compiler_type t = get_obj_compiler(wk, rcvr)->type;
	*res = make_str(wk, linker_type_to_s(compilers[t].linker));
	return true;
}

static bool
func_compiler_get_argument_syntax(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *syntax;
	enum compiler_type type = get_obj_compiler(wk, rcvr)->type;

	switch (type) {
	case compiler_posix:
	case compiler_gcc:
	case compiler_clang:
	case compiler_apple_clang:
		syntax = "gcc";
		break;
	default:
		syntax = "other";
		break;
	}

	*res = make_str(wk, syntax);
	return true;
}

struct compiler_find_library_ctx {
	char path[PATH_MAX];
	obj lib_name;
	bool only_static;
	bool found;
};

static enum iteration_result
compiler_find_library_iter(struct workspace *wk, void *_ctx, obj libdir)
{
	struct compiler_find_library_ctx *ctx = _ctx;
	char lib[PATH_MAX];
	static const char *pref[] = { "", "lib", NULL };
	const char *suf[] = { ".so", ".a", NULL };

	if (ctx->only_static) {
		suf[0] = ".a";
		suf[1] = NULL;
	}

	uint32_t i, j;
	for (i = 0; suf[i]; ++i) {
		for (j = 0; pref[j]; ++j) {
			snprintf(lib, PATH_MAX, "%s%s%s", pref[j], get_cstr(wk, ctx->lib_name), suf[i]);

			if (!path_join(ctx->path, PATH_MAX, get_cstr(wk, libdir), lib)) {
				return false;
			}

			if (fs_file_exists(ctx->path)) {
				ctx->found = true;
				return ir_done;
			}
		}
	}

	return ir_cont;
}

static bool
func_compiler_find_library(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_static,
		kw_disabler,
		kw_has_headers, // TODO
		kw_dirs,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required", tc_required_kw },
		[kw_static] = { "static", obj_bool },
		[kw_disabler] = { "disabler", obj_bool },
		[kw_has_headers] = { "has_headers", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_dirs] = { "dirs", ARG_TYPE_ARRAY_OF | obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum requirement_type requirement;
	if (!coerce_requirement(wk, &akw[kw_required], &requirement)) {
		return false;
	}

	make_obj(wk, res, obj_dependency);
	struct obj_dependency *dep = get_obj_dependency(wk, *res);
	dep->type = dependency_type_external_library;

	if (requirement == requirement_skip) {
		return true;
	}

	struct compiler_find_library_ctx ctx = {
		.lib_name = an[0].val,
		.only_static = akw[kw_static].set ? get_obj_bool(wk, akw[kw_static].val) : false,
	};
	struct obj_compiler *comp = get_obj_compiler(wk, rcvr);

	bool found_from_dirs_kw = false;

	if (akw[kw_dirs].set) {
		if (!obj_array_foreach(wk, akw[kw_dirs].val, &ctx, compiler_find_library_iter)) {
			return false;
		}

		if (ctx.found) {
			found_from_dirs_kw = true;
		}
	}

	if (!ctx.found) {
		if (!obj_array_foreach(wk, comp->libdirs, &ctx, compiler_find_library_iter)) {
			return false;
		}
	}

	if (!ctx.found) {
		if (requirement == requirement_required) {
			interp_error(wk, an[0].node, "library not found");
			return false;
		}

		LOG_W("library '%s' not found", get_cstr(wk, an[0].val));
		if (akw[kw_disabler].set && get_obj_bool(wk, akw[kw_disabler].val)) {
			*res = disabler_id;
		} else {
			return true;
		}
	} else {
		LOG_I("found library '%s' at '%s'", get_cstr(wk, an[0].val), ctx.path);
		dep->flags |= dep_flag_found;
		make_obj(wk, &dep->dep.link_with, obj_array);
		obj_array_push(wk, dep->dep.link_with, make_str(wk, ctx.path));

		if (found_from_dirs_kw) {
			make_obj(wk, &dep->dep.rpath, obj_array);
			obj_array_push(wk, dep->dep.rpath, make_str(wk, ctx.path));
		}

		dep->dep.link_language = comp->lang;
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
	obj_array_push(wk, *res, get_obj_compiler(wk, rcvr)->name);
	return true;
}

static bool
func_compiler_version(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_compiler(wk, rcvr)->ver;
	return true;
}

const struct func_impl_name impl_tbl_compiler[] = {
	{ "alignment", func_compiler_alignment, tc_number },
	{ "check_header", func_compiler_check_header, tc_bool },
	{ "cmd_array", func_compiler_cmd_array, tc_array },
	{ "compiles", func_compiler_compiles, tc_bool },
	{ "compute_int", func_compiler_compute_int, tc_number },
	{ "find_library", func_compiler_find_library, tc_dependency },
	{ "first_supported_argument", func_compiler_first_supported_argument, tc_array },
	{ "first_supported_link_argument", func_compiler_first_supported_link_argument, tc_array },
	{ "get_argument_syntax", func_compiler_get_argument_syntax, tc_string },
	{ "get_define", func_compiler_get_define, tc_string },
	{ "get_id", func_compiler_get_id, tc_string },
	{ "get_linker_id", func_compiler_get_linker_id, tc_string },
	{ "get_supported_arguments", func_compiler_get_supported_arguments, tc_array },
	{ "get_supported_function_attributes", func_compiler_get_supported_function_attributes, tc_array },
	{ "get_supported_link_arguments", func_compiler_get_supported_link_arguments, tc_array },
	{ "has_argument", func_compiler_has_argument, tc_bool },
	{ "has_function", func_compiler_has_function, tc_bool },
	{ "has_function_attribute", func_compiler_has_function_attribute, tc_bool },
	{ "has_header", func_compiler_has_header, tc_bool },
	{ "has_header_symbol", func_compiler_has_header_symbol, tc_bool },
	{ "has_link_argument", func_compiler_has_link_argument, tc_bool },
	{ "has_member", func_compiler_has_member, tc_bool },
	{ "has_members", func_compiler_has_members, tc_bool },
	{ "has_multi_arguments", func_compiler_has_multi_arguments, tc_bool },
	{ "has_multi_link_arguments", func_compiler_has_multi_link_arguments, tc_bool },
	{ "has_type", func_compiler_has_type, tc_bool },
	{ "links", func_compiler_links, tc_bool },
	{ "run", func_compiler_run, tc_run_result },
	{ "sizeof", func_compiler_sizeof, tc_number },
	{ "symbols_have_underscore_prefix", func_compiler_symbols_have_underscore_prefix, tc_bool },
	{ "version", func_compiler_version, tc_string },
	{ NULL, NULL },
};
