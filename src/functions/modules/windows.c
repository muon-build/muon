/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h> /* for include/lang/string.h */

#include "args.h"
/* #include "backend/common_args.h" */
/* #include "coerce.h" */
#include "error.h"
#include "functions/kernel/custom_target.h"
/* #include "functions/modules/windows.h" */
#include "lang/typecheck.h"
#include "options.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

enum rc_type {
	rc_type_windres, /* GNU windres */
	rc_type_rc,      /* Windows rc or llvm-rc */
	rc_type_wrc      /* wine rc */
};

struct module_windows_ctx {
	enum rc_type rc_type;
	obj command;
	obj args;
	obj depend_files;
	obj include_directories;
	obj *res;
	struct args_norm *an;
	const char *suffix;
};

static bool
module_find_resource_compiler(struct workspace *wk, struct module_windows_ctx *ctx)
{
	/*
	 * from https://mesonbuild.com/Windows-module.html#compile_resources
	 *
	 * The resource compiler executable used is the first which
	 * exists from the following list:
	 * 1. The windres executable given in the [binaries] section
	 *    of the cross-file
	 * 2. The WINDRES environment variable
	 * 3. The resource compiler which is part of the same toolset
	 *    as the C or C++ compiler in use.
	 */


	// TODO: get rc compiler from cross-compilation file first

	obj env_windres_arr_opt;
	const char *rc_str = NULL;
	enum compiler_type cc_type;
	enum rc_type rc_type;
	obj objcomp;
	bool has_no_logo = false;

	/* get compiler id */
	// FIXME : meson checks for linkers too
	if (obj_dict_geti(wk, current_project(wk)->compilers, compiler_language_c, &objcomp)) {

		cc_type = get_obj_compiler(wk, objcomp)->type[toolchain_component_compiler];
		// FIXME: add intel_cl compiler too
		// FIXME: add wine rc too
		// FIXME: try llvm-rc for lcang_cl
		switch (cc_type)
		{
		case compiler_gcc:
		case compiler_clang:
			rc_type = rc_type_windres;
			ctx->suffix = "o";
			break;
			rc_type = rc_type_windres;
			ctx->suffix = "o";
			break;
		case compiler_clang_cl:
		case compiler_msvc:
			rc_type = rc_type_rc;
			ctx->suffix = "res";
			break;
		default:
			vm_error(wk, "Unsupported compiler");
		}
	} else {
		vm_error(wk, "Could not find C or C++ compiler");
		return false;
	}

	// get rc compiler in env.WINDRES option

	if (get_option(wk, NULL, &WKSTR("env.WINDRES"), &env_windres_arr_opt)) {
		struct obj_option *env_windres_arr;

		env_windres_arr = get_obj_option(wk, env_windres_arr_opt);
		if (env_windres_arr->source > option_value_source_default) {
			obj env_windres;

			get_option_value(wk, NULL, "env.WINDRES", &env_windres);
			rc_str = get_cstr(wk, env_windres);
		}
	}

	// then get rc compiler in WINDRES environment variable

	if (!rc_str) {
		rc_str = getenv("WINDRES");
	}

	// if not found, get rc compiler based on compiler type

	if (!rc_str) {
		switch (cc_type)
		{
		case compiler_msvc:
			rc_str = "rc";
			has_no_logo = true;
			break;
		case compiler_clang_cl:
			rc_str = "llvm-rc";
			break;
		case compiler_gcc:
			/* fall through */
		case compiler_clang:
			rc_str = "windres";
			break;
		default:
			vm_error(wk, "Could not find appropriate environment for resource compiler");
			return false;
		}
	}

	/* print resource compiler version */
	struct run_cmd_ctx run_cmd_ctx = { 0 };
	char *run_cmd[] = {
		(char *)rc_str,
		(rc_type == rc_type_rc) ? "/?" : "--version",
		NULL
	};

	if (run_cmd_argv(&run_cmd_ctx, run_cmd, NULL, 0)) {
		char *buf = run_cmd_ctx.out.buf;

		// 'rc.exe /?' begins with \r\n
		while (*buf == '\r' || *buf == '\n') {
			buf++;
		}

		char *ver = strchr(buf, '\n');
		if (ver) {
			if (ver - buf > 0 && *(ver - 1) == '\r') {
				ver--;
			}
			*ver = '\0';
			LOG_I("Windows resource compiler: %s", buf);
		}
		run_cmd_ctx_destroy(&run_cmd_ctx);
	} else {
		vm_error(wk, "Could not find available resource compiler");
		return false;
	}

	ctx->rc_type = rc_type;

	const char *argv[2] = { NULL, NULL };
	struct args args = { .args = argv, .len = ARRAY_LEN(argv) };

	argv[0] = rc_str;
	if (has_no_logo) {
		argv[1] = "/nologo";
	} else {
		args.len = 1;
	}
	push_args(wk, ctx->command, &args);

	return true;
}

/* for debug */
static enum iteration_result
cmd_iter(struct workspace *wk, void *_ctx, obj val)
{
	switch (get_obj_type(wk, val)) {
	case obj_string:
		printf("* string: '%s'\n", get_cstr(wk, val));
		break;
	case obj_file:
		printf("* file: '%s'\n", get_file_path(wk, val));
		break;
	case obj_custom_target:
		printf("* custom target\n");
		// TODO
		break;
	default:
		UNREACHABLE;
	}

	return ir_cont;
}

static enum iteration_result
module_args_iter(struct workspace *wk, void *_ctx, obj val)
{
	const char *argv[1] = { NULL };
	struct args args = { .args = argv, .len = ARRAY_LEN(argv) };
	struct module_windows_ctx *ctx = _ctx;

	SBUF(an);
	sbuf_clear(&an);

	switch (get_obj_type(wk, val)) {
	case obj_string:
		sbuf_pushs(NULL, &an, get_cstr(wk, val));
		break;
	default:
		UNREACHABLE;
	}

	argv[0] = an.buf;
	push_args(wk, ctx->command, &args);

	sbuf_destroy(&an);

	return ir_cont;
}

static enum iteration_result
module_depend_files_iter(struct workspace *wk, void *_ctx, obj val)
{
	const char *argv[1] = { NULL };
	struct args args = { .args = argv, .len = ARRAY_LEN(argv) };
	struct module_windows_ctx *ctx = _ctx;

	switch (get_obj_type(wk, val)) {
	case obj_string:
		//printf(" ** depend file from string: '%s'\n", get_cstr(wk, val));
		argv[0] = get_cstr(wk, val);
		push_args(wk, ctx->depend_files, &args);
		break;
	case obj_file:
		//printf(" ** depend file from file: '%s'\n", get_file_path(wk, val));
		argv[0] = get_file_path(wk, val);
		push_args(wk, ctx->depend_files, &args);
		break;
	default:
		UNREACHABLE;
	}

	return ir_cont;
}

static enum iteration_result
module_include_directories_iter(struct workspace *wk, void *_ctx, obj val)
{
	const char *argv[1] = { NULL };
	struct args args = { .args = argv, .len = ARRAY_LEN(argv) };
	struct module_windows_ctx *ctx = _ctx;

	SBUF(incdir);
	sbuf_clear(&incdir);

	sbuf_pushs(NULL, &incdir, ctx->rc_type == rc_type_rc ? "/i" : "-I");

	switch (get_obj_type(wk, val)) {
	case obj_string:
		sbuf_pushs(NULL, &incdir, get_cstr(wk, val));
		break;
	case obj_include_directory: {
		struct obj_include_directory *inc = get_obj_include_directory(wk, val);
		sbuf_pushs(NULL, &incdir, get_cstr(wk, inc->path));
		break;
	}
	default:
		UNREACHABLE;
	}

	argv[0] = incdir.buf;
	push_args(wk, ctx->command, &args);

	sbuf_destroy(&incdir);

	return ir_cont;
}

/* prefix for output from rc file */
static void
module_prefix(struct workspace *wk, struct sbuf *buf, const char *path)
{
	SBUF(tmp)
	sbuf_clear(&tmp);
	if (path_is_absolute(path)) {
		path_relative_to(wk, &tmp, wk->build_root, path);
	} else {
		sbuf_pushs(NULL, &tmp, path);
	}

	/*
	 * remove the possible dots and (back)slash at the beginning
	 * of 'tmp.buf'
	 */
	char *iter = tmp.buf;
	while (*iter) {
		if (*iter == '.' || *iter == '/' || *iter == '\\') {
			iter++;
		} else {
			break;
		}
	}

	/* replace (back)slash with underscore */
	char *iter2 = iter;
	while (*iter2) {
		if (*iter2 == '/' || *iter2 == '\\') {
			*iter2 = '_';
		}
		iter2++;
	}

	sbuf_clear(buf);
	sbuf_pushs(wk, buf, iter);

	sbuf_destroy(&tmp);
}

/* basename without extension */
static void
module_basename(struct workspace *wk, struct sbuf *buf, const char *path)
{
	SBUF(tmp)
	sbuf_clear(&tmp);
	path_basename(wk, &tmp, path);

 	sbuf_clear(buf);
	path_without_ext(wk, buf, tmp.buf);

	sbuf_destroy(&tmp);
}

static enum iteration_result
module_an_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct module_windows_ctx *ctx = _ctx;
	obj cmd;
	const char *rc_file_tmp;

	obj_array_dup(wk, ctx->command, &cmd);
	/* printf("ctx->command dup : \n"); */
	/* fflush(stdout); */
	/* obj_array_foreach(wk, cmd, &ctx, cmd_iter); */

	switch (get_obj_type(wk, val)) {
	case obj_string:
		/* printf(" ** from string\n"); */
		rc_file_tmp = get_cstr(wk, val);
		break;
	case obj_file:
		/* printf(" ** from file\n"); */
		rc_file_tmp = get_file_path(wk, val);
		break;
	case obj_custom_target:
		/* printf("* custom target\n"); */
		// FIXME: to do
		break;
	default:
		UNREACHABLE;
	}

	/* prefix of rc_file for 'output' */
	SBUF(prefix);
	module_prefix(wk, &prefix, rc_file_tmp);

	/* basename of rc_file, without extention */
	SBUF(basename);
	module_basename(NULL, &basename, rc_file_tmp);

	/* if 'input' is
	 * ../foo/bar.ext
	 * 'output' name should be
	 * foo_bar.ext_bar.suffix */
	SBUF(output);
	sbuf_clear(&output);
	sbuf_pushs(NULL, &output, prefix.buf);
	//sbuf_pushs(NULL, &output, "_@BASENAME@.");
	sbuf_push(NULL, &output, '_');
	sbuf_pushs(NULL, &output, basename.buf);
	sbuf_push(NULL, &output, '.');
	sbuf_pushs(NULL, &output, ctx->suffix);

	SBUF(depfile);
	sbuf_clear(&depfile);
	sbuf_pushs(NULL, &depfile, output.buf);
	sbuf_pushn(NULL, &depfile, ".d", 2);

	/* { */
	/* 	printf(" *** current build dir : '%s'\n", get_cstr(wk, current_project(wk)->build_dir)); */
	/* 	printf(" *** full     : '%s'\n", rc_file_tmp); */
	/* 	printf(" *** prefix   : '%s'\n", prefix.buf); */
	/* 	printf(" *** basename : '%s'\n", basename.buf); */
	/* 	printf(" *** output   : '%s'\n", output.buf); */
	/* 	printf(" *** depfile  : '%s'\n", depfile.buf); */
	/* 	fflush(stdout); */
	/* } */

	switch (ctx->rc_type) {
	case rc_type_windres: {
		const char *argv[5] = { NULL, NULL, NULL, NULL, NULL };
		struct args args = { .args = argv, .len = ARRAY_LEN(argv) };

		/* --preprocessor-arg options */
		argv[0] = "--preprocessor-arg=-MD";

		SBUF(ppa_mq);
		sbuf_clear(&ppa_mq);
		sbuf_pushs(NULL, &ppa_mq, "--preprocessor-arg=-MQ@OUTPUT@");
		argv[1] = ppa_mq.buf;

		SBUF(ppa_mf);
		sbuf_clear(&ppa_mf);
		sbuf_pushs(NULL, &ppa_mf, "--preprocessor-arg=-MF@DEPFILE@");
		argv[2] = ppa_mf.buf;

		argv[3] = "@INPUT@";
		argv[4] = "@OUTPUT@";
		push_args(wk, cmd, &args);
		break;
	}
	case rc_type_rc:
	{
		const char *argv[2] = { NULL, NULL };
		struct args args = { .args = argv, .len = ARRAY_LEN(argv) };

		argv[0] = "@INPUT@";
		argv[1] = "/fo@OUTPUT@";
		push_args(wk, cmd, &args);
		break;
	}
	case rc_type_wrc:
	{
		const char *argv[3] = { NULL, NULL, NULL };
		struct args args = { .args = argv, .len = ARRAY_LEN(argv) };

		argv[0] = "@INPUT@";
		argv[1] = "-o@";
		argv[2] = "@OUTPUT@";
		push_args(wk, cmd, &args);
		break;
	}
	}

	SBUF(name);
	sbuf_clear(&name);
	sbuf_pushs(NULL, &name, rc_file_tmp);
        // Path separators are not allowed in target names
	char *iter = name.buf;
	while (*iter) {
		if (*iter == '/' || *iter == '\\' || *iter == ':') {
			*iter = '_';
		}
		iter++;
	}

	//obj_array_foreach(wk, cmd, &ctx, cmd_iter);

	struct make_custom_target_opts opts = {
		.name         = make_str(wk, "foobarfoobar"/*name.buf*/),
		.input_node   = ctx->an[0].node,
		.output_node  = ctx->an[1].node,
		.input_orig   = make_str(wk, rc_file_tmp),
		.output_orig  = make_str(wk, output.buf),
		.output_dir   = get_cstr(wk, current_project(wk)->build_dir),
		.command_orig = cmd,
		.depfile_orig = make_str(wk, depfile.buf),
	};

	if (!make_custom_target(wk, &opts, ctx->res)) {
		return ir_err;
	}

	obj_array_push(wk, current_project(wk)->targets, *ctx->res);

	sbuf_destroy(&name);
	sbuf_destroy(&basename);
	sbuf_destroy(&prefix);

	return ir_cont;
}

static bool
func_module_windows_compile_resources(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | tc_string | tc_file | tc_custom_target }, ARG_TYPE_NULL };
	enum {
		kw_args,
		kw_depend_files,
		kw_depends,
		kw_include_directories,
	};
	struct args_kw akw[] = {
		[kw_args] = { "args", TYPE_TAG_LISTIFY | tc_string },
		[kw_depend_files] = { "depend_files", TYPE_TAG_LISTIFY | tc_string | tc_file },
		[kw_depends] = { "depends", TYPE_TAG_LISTIFY | tc_build_target | tc_custom_target },
		[kw_include_directories] = { "include_directories", TYPE_TAG_LISTIFY | tc_coercible_inc },
		0
	};
	struct module_windows_ctx ctx = { 0 };

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	make_obj(wk, &ctx.command, obj_array);
	ctx.res = res;
	ctx.an = an;

	/*
	 * Find the Windows resource compiler (see this function for the
	 * order of the checks).
	 * The function fills ctx.command with the resource compiler
	 * It also appends '/nologo' to ctx.command with rc.exe compiler
	 */
	if (!module_find_resource_compiler(wk, &ctx)) {
		return false;
	}

	/*
	 * kw_args
	 * Append to ctx.command the values in kw_args
	 */
	if (akw[kw_args].set) {
		if (!obj_array_foreach(wk, akw[kw_args].val, &ctx, module_args_iter)) {
			return false;
		}
	}

	/*
	 * kw_depend_files
	 * Add dependency files
	 */
	if (akw[kw_depend_files].set) {
		make_obj(wk, &ctx.depend_files, obj_array);
		if (!obj_array_foreach(wk, akw[kw_depend_files].val, &ctx, module_depend_files_iter)) {
			return false;
		}
	}

	/*
	 * kw_include_directories
	 * Append to ctx.command the additional include directories
	 * (with '/i' or '-I', according to the resource compiler)
	 */
	if (akw[kw_include_directories].set) {
		if (!obj_array_foreach(wk, akw[kw_include_directories].val, &ctx, module_include_directories_iter)) {
			return false;
		}
	}

	if (!obj_array_foreach(wk, an[0].val, &ctx, module_an_iter)) {
		return false;
	}

	return true;
}

const struct func_impl impl_tbl_module_windows[] = {
	{ "compile_resources", func_module_windows_compile_resources, tc_array },
	{ NULL, NULL },
};
