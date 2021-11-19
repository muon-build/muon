#include "posix.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "buf_size.h"
#include "coerce.h"
#include "compilers.h"
#include "functions/common.h"
#include "functions/compiler.h"
#include "functions/dependency.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/dirs.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

static const char *
bool_to_yn(bool v)
{
	return v ? "\033[32mYES\033[0m" : "\033[31mNO\033[0m";
}

struct func_compiler_get_supported_arguments_iter_ctx {
	uint32_t arr, node, compiler;
};

static bool
write_test_source(struct workspace *wk, const struct str *src, const char **res)
{
	static char test_source_path[PATH_MAX];
	if (!path_join(test_source_path, PATH_MAX, wk->muon_private, "test.c")) {
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
	uint32_t err_node;
	const char *src;
	obj deps;
	obj args;
	bool skip_run_check;
};

static bool
compiler_check(struct workspace *wk, struct compiler_check_opts *opts, bool *res)
{
	struct obj *comp = get_obj(wk, opts->comp_id);
	const char *name = get_cstr(wk, comp->dat.compiler.name);
	enum compiler_type t = comp->dat.compiler.type;

	obj compiler_args;
	make_obj(wk, &compiler_args, obj_array);

	obj comp_name;
	make_obj(wk, &comp_name, obj_string)->dat.str = comp->dat.compiler.name;
	obj_array_push(wk, compiler_args, comp_name);
	push_args(wk, compiler_args, compilers[t].args.werror());

	if (opts->args) {
		obj args_dup;
		obj_array_dup(wk, opts->args, &args_dup);
		obj_array_extend(wk, compiler_args, args_dup);
	}

	const char *output = "/dev/null";
	if (opts->mode == compile_mode_run) {
		static char test_output_path[PATH_MAX];
		if (!path_join(test_output_path, PATH_MAX, wk->muon_private, "test")) {
			return false;
		}
		output = test_output_path;
	}

	push_args(wk, compiler_args, compilers[t].args.output(output));

	switch (opts->mode) {
	case compile_mode_preprocess:
		push_args(wk, compiler_args, compilers[t].args.preprocess_only());
		break;
	case compile_mode_compile:
		push_args(wk, compiler_args, compilers[t].args.compile_only());
		break;
	case compile_mode_run:
	case compile_mode_link:
		if (opts->deps) {
			struct dep_args_ctx da_ctx;
			dep_args_ctx_init(wk, &da_ctx);

			if (!deps_args(wk, opts->deps, &da_ctx)) {
				return false;
			}

			setup_linker_args(wk, NULL, compilers[t].linker,
				comp->dat.compiler.lang, da_ctx.rpath, da_ctx.link_args, da_ctx.link_with);
			obj_array_extend(wk, compiler_args, da_ctx.link_args);
		}
	}

	obj_array_push(wk, compiler_args, make_str(wk, opts->src));

	bool ret = false;
	struct run_cmd_ctx cmd_ctx = { 0 };

	const char *argv[MAX_ARGS] = { name };
	if (!join_args_argv(wk, argv, MAX_ARGS, compiler_args)) {
		return false;
	}

	L("compiling: '%s'", opts->src);

	if (!run_cmd(&cmd_ctx, name, argv, NULL)) {
		interp_error(wk, opts->err_node, "error: %s", cmd_ctx.err_msg);
		goto ret;
	}

	L("compiler stdout: '%s'", cmd_ctx.err.buf);
	L("compiler stderr: '%s'", cmd_ctx.out.buf);

	if (opts->mode == compile_mode_run) {
		if (cmd_ctx.status != 0) {
			LOG_W("failed to compile test, rerun with -v to see compiler invocation");
			goto ret;
		}

		if (!run_cmd(&opts->cmd_ctx, output, (const char *const []){
			output, NULL
		}, NULL)) {
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
	return ret;
}

static bool
func_compiler_sizeof(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_args,
		kw_dependencies,
		kw_prefix,
	};
	struct args_kw akw[] = {
		[kw_args] = { "args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_dependency },
		[kw_prefix] = { "prefix", obj_string },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	const char *prefix = akw[kw_prefix].set ? get_cstr(wk, akw[kw_prefix].val) : "";

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"#include <stdio.h>\n"
		"%s\n"
		"int main(void) { printf(\"%%ld\", (long)(sizeof(%s))); }\n",
		prefix,
		get_cstr(wk, an[0].val)
		);

	const char *path;
	if (!write_test_source(wk, &WKSTR(src), &path)) {
		return false;
	}

	struct compiler_check_opts opts = {
		.mode = compile_mode_run,
		.comp_id = rcvr,
		.err_node = an[0].node,
		.src = path,
		.deps = akw[kw_dependencies].val,
		.args = akw[kw_args].val,
	};

	bool ret = false;

	int64_t size = -1;
	bool ok;
	if (!compiler_check(wk, &opts, &ok) || !ok) {
		goto ret;
	}

	char *endptr;
	size = strtol(opts.cmd_ctx.out.buf, &endptr, 10);
	if (*endptr) {
		LOG_W("sizeof binary had malformed output '%s'", opts.cmd_ctx.out.buf);
		size = -1;
	}

	ret = true;
ret:
	make_obj(wk, res, obj_number)->dat.num = size;
	run_cmd_ctx_destroy(&opts.cmd_ctx);

	LOG_I("sizeof %s: %ld",
		get_cstr(wk, an[0].val),
		(long)size
		);

	return ret;
}

static bool
func_compiler_alignment(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_args,
		kw_dependencies,
		kw_prefix,
	};
	struct args_kw akw[] = {
		[kw_args] = { "args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_dependency },
		[kw_prefix] = { "prefix", obj_string },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	const char *prefix = akw[kw_prefix].set ? get_cstr(wk, akw[kw_prefix].val) : "";

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"#include <stdio.h>\n"
		"#include <stddef.h>\n"
		"%s\n"
		"struct tmp { char c; %s target; };\n"
		"int main(void) { printf(\"%%d\", (int)(offsetof(struct tmp, target))); }\n",
		prefix,
		get_cstr(wk, an[0].val)
		);

	const char *path;
	if (!write_test_source(wk, &WKSTR(src), &path)) {
		return false;
	}

	struct compiler_check_opts opts = {
		.mode = compile_mode_run,
		.comp_id = rcvr,
		.err_node = an[0].node,
		.src = path,
		.deps = akw[kw_dependencies].val,
		.args = akw[kw_args].val,
	};

	bool ret = false;

	int64_t num = -1;
	bool ok;
	if (!compiler_check(wk, &opts, &ok) || !ok) {
		goto ret;
	}

	char *endptr;
	num = strtol(opts.cmd_ctx.out.buf, &endptr, 10);
	if (*endptr) {
		LOG_W("alignment binary had malformed output '%s'", opts.cmd_ctx.out.buf);
		num = -1;
	}

	ret = true;
ret:
	make_obj(wk, res, obj_number)->dat.num = num;
	run_cmd_ctx_destroy(&opts.cmd_ctx);

	LOG_I("alignment of %s: %ld",
		get_cstr(wk, an[0].val),
		(long)num
		);

	return ret;
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
		  "int foo(void) { return 0; }\n"
		  "int bar(void) __attribute__((alias(\"foo\")));\n" },
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
		if (wk_cstreql(name, tests[i].name)) {
			*res = tests[i].src;
			return true;
		}
	}

	return false;
}

static bool
func_compiler_has_function_attribute(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *src;
	if (!get_has_function_attribute_test(get_str(wk, an[0].val), &src)) {
		interp_error(wk, an[0].node, "unknown attribute '%s'", get_cstr(wk, an[0].val));
		return false;
	}

	const char *path;
	if (!write_test_source(wk, &WKSTR(src), &path)) {
		return false;
	}

	struct compiler_check_opts opts = {
		.mode = compile_mode_compile,
		.comp_id = rcvr,
		.err_node = an[0].node,
		.src = path,
	};

	bool ret = false;
	bool ok = false;
	if (!compiler_check(wk, &opts, &ok) || !ok) {
		goto ret;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = ok;
	ret = true;
ret:
	LOG_I("have attribute %s: %s",
		get_cstr(wk, an[0].val),
		bool_to_yn(ok)
		);

	return ret;
}

static bool
func_compiler_has_function(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_dependencies,
		kw_prefix,
	};
	struct args_kw akw[] = {
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_dependency },
		[kw_prefix] = { "prefix", obj_string },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	const char *prefix = akw[kw_prefix].set ? get_cstr(wk, akw[kw_prefix].val) : "";

	char src[BUF_SIZE_4k];
	if (strstr(prefix, "#include")) {
		snprintf(src, BUF_SIZE_4k,
			"%s\n"
			"int main(void) {\n"
			"void *a = (void*) &%s;\n"
			"long long b = (long long) a;\n"
			"return (int) b;\n"
			"}\n",
			prefix,
			get_cstr(wk, an[0].val)
			);
	} else {
		snprintf(src, BUF_SIZE_4k,
			"%s\n"
			"char %s (void);\n"
			"int main(void) { return %s(); }\n",
			prefix,
			get_cstr(wk, an[0].val),
			get_cstr(wk, an[0].val)
			);
	}

	const char *path;
	if (!write_test_source(wk, &WKSTR(src), &path)) {
		return false;
	}

	struct compiler_check_opts opts = {
		.mode = compile_mode_link,
		.comp_id = rcvr,
		.err_node = an[0].node,
		.src = path,
		.deps = akw[kw_dependencies].val,
	};

	bool ok;
	if (!compiler_check(wk, &opts, &ok)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = ok;
	return true;
}

static bool
func_compiler_has_header_symbol(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_dependencies,
		kw_prefix,
	};
	struct args_kw akw[] = {
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_dependency },
		[kw_prefix] = { "prefix", obj_string },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	const char *prefix = akw[kw_prefix].set ? get_cstr(wk, akw[kw_prefix].val) : "";

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
		get_cstr(wk, an[0].val),
		get_cstr(wk, an[1].val),
		get_cstr(wk, an[1].val)
		);

	const char *path;
	if (!write_test_source(wk, &WKSTR(src), &path)) {
		return false;
	}

	struct compiler_check_opts opts = {
		.mode = compile_mode_link,
		.comp_id = rcvr,
		.err_node = an[0].node,
		.src = path,
		.deps = akw[kw_dependencies].val,
	};

	bool ok;
	if (!compiler_check(wk, &opts, &ok)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = ok;
	return true;
}

static bool
func_compiler_compiles(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	bool ret = false;
	struct args_norm an[] = { { obj_any }, ARG_TYPE_NULL };
	enum kwargs {
		kw_dependencies,
		kw_name,
		kw_args,
	};
	struct args_kw akw[] = {
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_dependency },
		[kw_name] = { "name", obj_string },
		[kw_args] = { "args", ARG_TYPE_ARRAY_OF | obj_string },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	obj o;
	if (!obj_array_flatten_one(wk, an[0].val, &o)) {
		interp_error(wk, an[0].node, "could not flatten argument");
	}

	struct obj *src_obj = get_obj(wk, o);

	const char *path;

	switch (src_obj->type) {
	case obj_string:
		if (!write_test_source(wk, get_str(wk, src_obj->dat.str), &path)) {
			return false;
		}
		break;
	case obj_file: {
		path = get_cstr(wk, src_obj->dat.file);
		break;
	}
	default:
		interp_error(wk, an[0].node, "expected file or string, got %s", obj_type_to_s(src_obj->type));
		return false;
	}

	struct compiler_check_opts opts = {
		.mode = compile_mode_compile,
		.comp_id = rcvr,
		.err_node = an[0].node,
		.src = path,
		.deps = akw[kw_dependencies].val,
		.args = akw[kw_args].val,
	};

	bool ok;
	if (!compiler_check(wk, &opts, &ok)) {
		goto ret;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = ok;
	ret = true;
ret:
	if (akw[kw_name].set) {
		LOG_I("%s compiles: %s",
			get_cstr(wk, akw[kw_name].val),
			bool_to_yn(ok)
			);
	}

	return ret;
}

static bool
func_compiler_has_header(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	bool ret = false;
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_args,
		kw_dependencies,
		kw_prefix,
		kw_required,
	};
	struct args_kw akw[] = {
		[kw_args] = { "args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_dependency },
		[kw_prefix] = { "prefix", obj_string },
		[kw_required] = { "required", obj_any },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum requirement_type req = requirement_auto;
	if (akw[kw_required].set) {
		if (!coerce_requirement(wk, &akw[kw_required], &req)) {
			return false;
		}
	}

	if (req == requirement_skip) {
		make_obj(wk, res, obj_bool)->dat.boolean = false;
		return true;
	}

	const char *prefix = akw[kw_prefix].set ? get_cstr(wk, akw[kw_prefix].val) : "";

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"%s\n"
		"#include <%s>\n"
		"int main(void) {}\n",
		prefix,
		get_cstr(wk, an[0].val)
		);

	const char *path;
	if (!write_test_source(wk, &WKSTR(src), &path)) {
		return false;
	}
	struct compiler_check_opts opts = {
		.mode = compile_mode_preprocess,
		.comp_id = rcvr,
		.err_node = an[0].node,
		.src = path,
		.deps = akw[kw_dependencies].val,
		.args = akw[kw_args].val,
	};

	bool ok;
	if (!compiler_check(wk, &opts, &ok)) {
		goto ret;
	}

	if (!ok && req == requirement_required) {
		interp_error(wk, an[0].node, "required header %s not found", get_cstr(wk, an[0].val));
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = ok;
	ret = true;
ret:
	LOG_I("header %s found: %s",
		get_cstr(wk, an[0].val),
		bool_to_yn(ok)
		);

	return ret;
}

static bool
func_compiler_has_type(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	bool ret = false;
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_args,
		kw_dependencies,
		kw_prefix,
	};
	struct args_kw akw[] = {
		[kw_args] = { "args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_dependency },
		[kw_prefix] = { "prefix", obj_string },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	const char *prefix = akw[kw_prefix].set ? get_cstr(wk, akw[kw_prefix].val) : "";

	char src[BUF_SIZE_4k];
	snprintf(src, BUF_SIZE_4k,
		"%s\n"
		"void bar(void) { sizeof(%s); }\n",
		prefix,
		get_cstr(wk, an[0].val)
		);

	const char *path;
	if (!write_test_source(wk, &WKSTR(src), &path)) {
		return false;
	}

	struct compiler_check_opts opts = {
		.mode = compile_mode_compile,
		.comp_id = rcvr,
		.err_node = an[0].node,
		.src = path,
		.deps = akw[kw_dependencies].val,
		.args = akw[kw_args].val,
	};

	bool ok;
	if (!compiler_check(wk, &opts, &ok)) {
		goto ret;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = ok;
	ret = true;
ret:
	LOG_I("has type %s: %s",
		get_cstr(wk, an[0].val),
		bool_to_yn(ok)
		);

	return ret;
}

static bool
compiler_has_argument(struct workspace *wk, obj comp_id, uint32_t err_node, obj arg, bool *has_argument)
{
	if (!typecheck(wk, err_node, arg, obj_string)) {
		return ir_err;
	}

	const char *path;
	if (!write_test_source(wk, &WKSTR("int main(void){}\n"), &path)) {
		return false;
	}

	obj args;
	make_obj(wk, &args, obj_array);
	obj_array_push(wk, args, arg);

	struct compiler_check_opts opts = {
		.mode = compile_mode_compile,
		.comp_id = comp_id,
		.err_node = err_node,
		.src = path,
		.args = args,
	};

	if (!compiler_check(wk, &opts, has_argument)) {
		return false;
	}

	LOG_I("'%s' supported: %s",
		get_cstr(wk, arg),
		bool_to_yn(*has_argument)
		);

	return true;
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
	struct args_norm an[] = { { ARG_TYPE_GLOB }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_array);

	return obj_array_foreach_flat(wk, an[0].val,
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
		kw_disabler,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required", obj_any },
		[kw_static] = { "static", obj_bool },
		[kw_disabler] = { "disabler", obj_bool },
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
			snprintf(lib, PATH_MAX, "lib%s.%s", get_cstr(wk, an[0].val), suf[j]);

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

		LOG_W("library '%s' not found", get_cstr(wk, an[0].val));
		if (akw[kw_disabler].set && get_obj(wk, akw[kw_disabler].val)->dat.boolean) {
			*obj = disabler_id;
		} else {
			make_obj(wk, obj, obj_external_library)->dat.external_library.found = false;
		}
	} else {
		LOG_I("found library '%s' at '%s'", get_cstr(wk, an[0].val), path);
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

static bool
func_compiler_version(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj(wk, rcvr)->dat.compiler.ver;
	return true;
}

const struct func_impl_name impl_tbl_compiler[] = {
	{ "alignment", func_compiler_alignment },
	{ "cmd_array", func_compiler_cmd_array },
	{ "compiles", func_compiler_compiles },
	{ "find_library", func_compiler_find_library },
	{ "get_id", func_compiler_get_id },
	{ "get_supported_arguments", func_compiler_get_supported_arguments },
	{ "has_argument", func_compiler_has_argument },
	{ "has_function", func_compiler_has_function },
	{ "has_function_attribute", func_compiler_has_function_attribute },
	{ "has_header", func_compiler_has_header },
	{ "has_header_symbol", func_compiler_has_header_symbol },
	{ "has_type", func_compiler_has_type },
	{ "sizeof", func_compiler_sizeof },
	{ "version", func_compiler_version },
	{ NULL, NULL },
};
