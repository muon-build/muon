/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@thunix.net> SPDX-FileCopyrightText: Luke Drummond <ldrumm@rtps.co>
 * SPDX-FileCopyrightText: Eli Schwartz <eschwartz@archlinux.org>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "buf_size.h"
#include "coerce.h"
#include "error.h"
#include "formats/ansi.h"
#include "functions/compiler.h"
#include "functions/kernel/custom_target.h"
#include "functions/kernel/dependency.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "toolchains.h"

MUON_ATTR_FORMAT(printf, 3, 4)
static void
compiler_log(struct workspace *wk, obj compiler, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	struct obj_compiler *comp = get_obj_compiler(wk, compiler);
	LLOG_I("%s: ", compiler_log_prefix(wk, comp->lang, comp->machine));
	log_printv(log_info, fmt, args);
	log_plain(log_info, "\n");

	va_end(args);
}

MUON_ATTR_FORMAT(printf, 3, 4)
static void
compiler_check_log(struct workspace *wk, struct compiler_check_opts *opts, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	struct obj_compiler *comp = get_obj_compiler(wk, opts->comp_id);
	LLOG_I("%s: ", compiler_log_prefix(wk, comp->lang, comp->machine));
	log_printv(log_info, fmt, args);

	if (opts->from_cache) {
		log_plain(log_info, " " CLR(c_cyan) "cached" CLR(0));
	}

	log_plain(log_info, "\n");

	va_end(args);
}

static bool
add_include_directory_args(struct workspace *wk,
	struct args_kw *inc,
	struct build_dep *dep,
	obj comp_id,
	obj compiler_args)
{
	obj include_dirs;
	include_dirs = make_obj(wk, obj_array);

	if (inc && inc->set) {
		obj includes;
		if (!coerce_include_dirs(wk, inc->node, inc->val, false, &includes)) {
			return false;
		}
		obj_array_extend_nodup(wk, include_dirs, includes);
	}

	if (dep) {
		obj_array_extend_nodup(wk, include_dirs, dep->include_directories);
	}

	ca_setup_compiler_args_includes(wk, comp_id, include_dirs, compiler_args, false);
	return true;
}

static bool
compiler_check_impl(struct workspace *wk, struct compiler_check_opts *opts, obj src_, uint32_t err_node, bool *res)
{
	const char *src = get_cstr(wk, src_);
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

	obj comp = opts->comp_id;
	struct obj_compiler *compiler = get_obj_compiler(wk, comp);

	// Set up compiler arguments

	obj compiler_args;
	compiler_args = make_obj(wk, obj_array);

	obj_array_extend(wk, compiler_args, compiler->cmd_arr[toolchain_component_compiler]);

	obj_array_extend(wk, compiler_args, toolchain_compiler_always(wk, comp));

	ca_get_std_args(wk, comp, current_project(wk), NULL, compiler_args);

	// if (compiler->lang == compiler_language_cpp) {
		obj_array_extend(wk, compiler_args, toolchain_compiler_permissive(wk, comp));
	// }

	if (opts->werror && opts->werror->set && get_obj_bool(wk, opts->werror->val)) {
		obj_array_extend(wk, compiler_args, toolchain_compiler_werror(wk, comp));
	}

	switch (opts->mode) {
	case compiler_check_mode_run:
	case compiler_check_mode_link:
		obj_array_extend(wk, compiler_args, toolchain_linker_fuse_ld(wk, comp));
		ca_get_option_link_args(wk, comp, current_project(wk), NULL, compiler_args);
	/* fallthrough */
	case compiler_check_mode_compile:
	case compiler_check_mode_preprocess:
		ca_get_option_compile_args(wk, compiler->lang, current_project(wk), NULL, compiler_args);
		break;
	}

	bool have_dep = false;
	struct build_dep dep = { 0 };
	if (opts->deps && opts->deps->set) {
		have_dep = true;
		dep_process_deps(wk, opts->deps->val, &dep);

		obj_array_extend_nodup(wk, compiler_args, dep.compile_args);
	}

	if (!add_include_directory_args(wk, opts->inc, have_dep ? &dep : NULL, opts->comp_id, compiler_args)) {
		return false;
	}

	obj source_path;
	if (opts->src_is_path) {
		source_path = make_str(wk, src);
	} else {
		TSTR(test_source_path);
		path_join(wk, &test_source_path, wk->muon_private, "test.");
		tstr_pushs(wk, &test_source_path, compiler_language_extension(wk, compiler->lang));
		source_path = tstr_into_str(wk, &test_source_path);
	}

	obj_array_push(wk, compiler_args, source_path);

	if (opts->mode == compiler_check_mode_preprocess) {
		obj_array_extend(wk, compiler_args, toolchain_compiler_preprocess_only(wk, comp));
	} else if (opts->mode == compiler_check_mode_compile) {
		obj_array_extend(wk, compiler_args, toolchain_compiler_compile_only(wk, comp));
	}

	TSTR(test_output_path);
	const char *output_path = 0;
	if (!opts->output_is_stdout) {
		if (opts->output_path) {
			output_path = opts->output_path;
		} else if (opts->mode == compiler_check_mode_run) {
			path_join(wk, &test_output_path, wk->muon_private, "compiler_check_exe");
			if (machine_definitions[compiler->machine]->is_windows) {
				tstr_pushs(wk, &test_output_path, ".exe");
			}
			output_path = test_output_path.buf;
		} else {
			path_join(wk, &test_output_path, wk->muon_private, "test.");
			tstr_pushs(wk, &test_output_path, compiler_language_extension(wk, compiler->lang));
			const char *ext
				= toolchain_compiler_flatten_one(wk, comp, toolchain_compiler_object_ext(wk, comp));
			if (!ext) {
				return false;
			}
			tstr_pushs(wk, &test_output_path, ext);
			output_path = test_output_path.buf;
		}

		obj_array_extend(wk, compiler_args, toolchain_compiler_output(wk, comp, output_path));
	}

	// Set up linker arguments
	if (opts->mode == compiler_check_mode_link || opts->mode == compiler_check_mode_run) {
		obj_array_extend(wk, compiler_args, toolchain_compiler_linker_delimiter(wk, comp));

		obj_array_extend(wk,
			compiler_args,
			toolchain_compiler_linker_passthrough(wk, comp, toolchain_linker_fatal_warnings(wk, comp)));

		if (have_dep) {
			struct obj_build_target tgt = {
				.dep_internal = dep,
			};
			ca_prepare_target_linker_args(wk, comp, current_project(wk), &tgt, false);
			obj_array_extend_nodup(wk, compiler_args, dep.link_args);
		}
	}

	// Add additional args here.  For compilers that support a linker delimiter
	// (cl.exe) they will be treated as linker arguments if mode is
	// compiler_check_mode_link.
	if (opts->args) {
		obj_array_extend(wk, compiler_args, opts->args);
	}

	if (opts->mode == compiler_check_mode_link || opts->mode == compiler_check_mode_run) {
		ca_link_args_to_native(wk, comp, &compiler_args);
	}

	bool ret = false;
	struct run_cmd_ctx cmd_ctx = { 0 };

	const char *argstr;
	uint32_t argc;
	join_args_argstr(wk, &argstr, &argc, compiler_args);

	opts->cache_key = compiler_check_cache_key(wk,
		&(struct compiler_check_cache_key){
			.comp = compiler,
			.argstr = argstr,
			.argc = argc,
			.src = src,
		});

	struct compiler_check_cache_value cache_value = { 0 };

	if (compiler_check_cache_get(wk, opts->cache_key, &cache_value)) {
		*res = cache_value.success;
		opts->cache_val = cache_value.value;
		opts->from_cache = true;
		return true;
	}

	if (!opts->src_is_path) {
		L("compiling: '%s'", src);

		if (!fs_write_entire_file(get_cstr(wk, source_path), (const uint8_t *)src, strlen(src))) {
			return false;
		}
	} else {
		L("compiling: '%s'", get_cstr(wk, source_path));
	}

	if (!run_cmd(wk, &cmd_ctx, argstr, argc, NULL, 0)) {
		vm_error_at(wk, err_node, "error: %s", cmd_ctx.err_msg);
		goto ret;
	}

	if (!opts->dont_log_compiler_output) {
		const uint32_t truncate_limit = BUF_SIZE_4k;
		const struct str truncated_sep = STR("'\n" CLR(c_cyan) "[truncated]" CLR(0) "\n'");
		LL("compiler stdout: '");
		log_print_middle_truncated(log_debug, &TSTR_STR(&cmd_ctx.out), &truncated_sep, truncate_limit);
		log_printn(log_debug, "'\n", 2);

		LL("compiler stderr: '");
		log_print_middle_truncated(log_debug, &TSTR_STR(&cmd_ctx.err), &truncated_sep, truncate_limit);
		log_printn(log_debug, "'\n", 2);
	}

	if (opts->mode == compiler_check_mode_run) {
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

		bool ran_cmd = false;
		bool run_cmd_ok = false;
		if (compiler->machine == machine_kind_host) {
			obj exe_wrapper;
			get_option_value(wk, current_project(wk), "b_host_exe_wrapper", &exe_wrapper);
			if (get_obj_array(wk, exe_wrapper)->len) {
				obj args;
				obj_array_dup(wk, exe_wrapper, &args);
				obj_array_push(wk, args, make_str(wk, output_path));
				const char *argstr;
				uint32_t argc;
				join_args_argstr(wk, &argstr, &argc, args);
				run_cmd_ok = run_cmd(wk, &opts->cmd_ctx, argstr, argc, 0, 0);
				ran_cmd = true;
			}
		}

		if (!ran_cmd) {
			run_cmd_ok = run_cmd_argv(wk, &opts->cmd_ctx, (char *const[]){ (char *)output_path, NULL }, NULL, 0);
		}

		if (!run_cmd_ok) {
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

	// store wether or not the check suceeded in the cache, the caller is
	// responsible for storing the actual value
	compiler_check_cache_set(wk, opts->cache_key, &(struct compiler_check_cache_value){ .success = *res });

	ret = true;
ret:
	if (ret && opts->keep_cmd_ctx) {
		opts->cmd_ctx = cmd_ctx;
	} else {
		run_cmd_ctx_destroy(&cmd_ctx);
	}
	if (!*res && req == requirement_required) {
		assert(opts->required);
		vm_error_at(wk, opts->required->node, "a required compiler check failed");
		return false;
	}
	return ret;
}

bool
compiler_check(struct workspace *wk, struct compiler_check_opts *opts, obj src, uint32_t err_node, bool *res)
{
	if (get_obj_type(wk, src) == obj_string) {
		return compiler_check_impl(wk, opts, src, err_node, res);
	}

	obj v;
	obj_array_for(wk, src, v) {
		if (!compiler_check_impl(wk, opts, v, err_node, res)) {
			return false;
		}

		if (*res) {
			return true;
		}
	}
	return true;
}

static int64_t
compiler_check_parse_output_int(struct compiler_check_opts *opts)
{
	char *endptr;
	int64_t size;
	size = strtoll(opts->cmd_ctx.out.buf, &endptr, 10);
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
	cc_kw_werror,

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
	cm_kw_werror = 1 << 9,
};

static void
compiler_opts_init(obj self, struct args_kw *akw, struct compiler_check_opts *opts)
{
	opts->comp_id = self;
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

	if (akw[cc_kw_werror].set) {
		opts->werror = &akw[cc_kw_werror];
	}

	if (akw[cc_kw_prefix].set) {
		opts->prefix = &akw[cc_kw_prefix];
	}
}

static bool
func_compiler_check_args_common(struct workspace *wk,
	obj self,
	struct args_norm *an,
	struct args_kw **kw_res,
	struct compiler_check_opts *opts,
	enum cc_kwargs args_mask)
{
	static struct args_kw akw[cc_kwargs_count + 1] = { 0 };
	struct args_kw akw_base[] = {
		[cc_kw_args] = { "args", TYPE_TAG_LISTIFY | obj_string },
		[cc_kw_dependencies] = { "dependencies", TYPE_TAG_LISTIFY | tc_dependency },
		[cc_kw_prefix] = { "prefix", TYPE_TAG_LISTIFY | obj_string },
		[cc_kw_required] = { "required", tc_required_kw },
		[cc_kw_include_directories] = { "include_directories", TYPE_TAG_LISTIFY | tc_coercible_inc },
		[cc_kw_name] = { "name", obj_string },
		[cc_kw_guess] = { "guess", obj_number, },
		[cc_kw_high] = { "high", obj_number, },
		[cc_kw_low] = { "low", obj_number, },
		[cc_kw_werror] = { "werror", obj_bool },
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

	if (!pop_args(wk, an, use_akw)) {
		return false;
	}

	if (use_akw) {
		uint32_t i;
		for (i = 0; i < cc_kwargs_count; ++i) {
			if ((args_mask & (1 << i))) {
				continue;
			} else if (akw[i].set) {
				vm_error_at(wk, akw[i].node, "invalid keyword '%s'", akw[i].key);
				return false;
			}
		}
	}

	compiler_opts_init(self, akw, opts);
	return true;
}

static const struct str compiler_check_delimiter_start = STR("\"MUON_GET_DEFINE_DELIMITER_START\"");
static const struct str compiler_check_delimiter_end = STR("\"MUON_GET_DEFINE_DELIMITER_END\"");
static const struct str compiler_check_delimiter_sentinel = STR("\"MUON_GET_DEFINE_UNDEFINED_SENTINEL\"");

struct args_kw *
compiler_check_snippet_kwargs(struct workspace *wk, struct compiler_check_opts *opts)
{
#define push_kw(...) kwargs_arr_push(wk, &kwargs, &(struct args_kw){ __VA_ARGS__ })
	struct arr kwargs = { 0 };
	kwargs_arr_init(wk, &kwargs);
	{
		obj prefix = 0;
		if (opts->prefix && opts->prefix->set) {
			if (get_obj_type(wk, opts->prefix->val) == obj_array) {
				obj joined;
				obj_array_join(wk, true, opts->prefix->val, make_str(wk, "\n"), &joined);
				opts->prefix->val = joined;
			}

			prefix = opts->prefix->val;
		} else {
			prefix = make_str(wk, "");
		}
		push_kw("prefix", .val = prefix, .allow_not_found = true);
	}
	if (vm_enum(wk, enum compiler_check_mode)) {
		vm_enum_value_prefixed(wk, compiler_check_mode, preprocess);
		vm_enum_value_prefixed(wk, compiler_check_mode, compile);
		vm_enum_value_prefixed(wk, compiler_check_mode, link);
		vm_enum_value_prefixed(wk, compiler_check_mode, run);
	}
	push_kw("mode", .val = vm_enum_to_obj(wk, enum compiler_check_mode, opts->mode), .allow_not_found = true);
	push_kw("delimiter_start", .val = make_strs(wk, &compiler_check_delimiter_start), .allow_not_found = true);
	push_kw("delimiter_end", .val = make_strs(wk, &compiler_check_delimiter_end), .allow_not_found = true);
	push_kw("delimiter_sentinel",
		.val = make_strs(wk, &compiler_check_delimiter_sentinel),
		.allow_not_found = true);
	return (struct args_kw *)kwargs.e;
}

#define compiler_handle_has_required_kw_setup(__requirement, kw)        \
	enum requirement_type __requirement;                            \
	if (!akw[kw].set) {                                             \
		__requirement = requirement_auto;                       \
	} else if (!coerce_requirement(wk, &akw[kw], &__requirement)) { \
		return false;                                           \
	}                                                               \
	if (__requirement == requirement_skip) {                        \
		*res = make_obj_bool(wk, false);                        \
		return true;                                            \
	}

#define compiler_handle_has_required_kw(__requirement, __result)  \
	if (__requirement == requirement_required && !__result) { \
		vm_error(wk, "required compiler check failed");   \
		return false;                                     \
	}

FUNC_IMPL(compiler, sizeof, tc_number, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compiler_check_mode_run,
		.skip_run_check = true,
	};

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_include_directories)) {
		return false;
	}

	obj src = language_snippet_sizeof(
		wk, get_obj_compiler(wk, self)->lang, an[0].val, compiler_check_snippet_kwargs(wk, &opts));

	bool ok;
	if (compiler_check(wk, &opts, src, an[0].node, &ok) && ok) {
		if (!opts.from_cache) {
			*res = make_obj(wk, obj_number);
			set_obj_number(wk, *res, compiler_check_parse_output_int(&opts));
		}
	} else {
		if (!opts.from_cache) {
			*res = make_obj(wk, obj_number);
			set_obj_number(wk, *res, -1);
		}
	}

	if (opts.from_cache) {
		*res = opts.cache_val;
	} else {
		run_cmd_ctx_destroy(&opts.cmd_ctx);

		compiler_check_cache_set(
			wk, opts.cache_key, &(struct compiler_check_cache_value){ .success = true, .value = *res });
	}

	compiler_check_log(wk, &opts, "sizeof %s: %" PRId64, get_cstr(wk, an[0].val), get_obj_number(wk, *res));

	return true;
}

FUNC_IMPL(compiler, alignment, tc_number, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compiler_check_mode_run,
	};

	if (!func_compiler_check_args_common(
		    wk, self, an, &akw, &opts, cm_kw_args | cm_kw_dependencies | cm_kw_prefix)) {
		return false;
	}

	obj src = language_snippet_alignment(
		wk, get_obj_compiler(wk, self)->lang, an[0].val, compiler_check_snippet_kwargs(wk, &opts));

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok) || !ok) {
		return false;
	}

	if (opts.from_cache) {
		*res = opts.cache_val;
	} else {
		*res = make_obj(wk, obj_number);
		set_obj_number(wk, *res, compiler_check_parse_output_int(&opts));
		run_cmd_ctx_destroy(&opts.cmd_ctx);
		compiler_check_cache_set(
			wk, opts.cache_key, &(struct compiler_check_cache_value){ .success = true, .value = *res });
	}

	compiler_check_log(wk, &opts, "alignment of %s: %" PRId64, get_cstr(wk, an[0].val), get_obj_number(wk, *res));

	return true;
}

FUNC_IMPL(compiler, compute_int, tc_number, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compiler_check_mode_run,
	};

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_include_directories | cm_kw_guess
			    | cm_kw_high | cm_kw_low)) {
		return false;
	}

	obj src = language_snippet_compute_int(
		wk, get_obj_compiler(wk, self)->lang, an[0].val, compiler_check_snippet_kwargs(wk, &opts));

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok) || !ok) {
		return false;
	}

	if (opts.from_cache) {
		*res = opts.cache_val;
	} else {
		*res = make_obj(wk, obj_number);
		set_obj_number(wk, *res, compiler_check_parse_output_int(&opts));
		run_cmd_ctx_destroy(&opts.cmd_ctx);
		compiler_check_cache_set(
			wk, opts.cache_key, &(struct compiler_check_cache_value){ .success = true, .value = *res });
	}

	compiler_check_log(wk, &opts, "%s computed to %" PRId64, get_cstr(wk, an[0].val), get_obj_number(wk, *res));
	return true;
}

static bool
compiler_has_function_attribute(struct workspace *wk, obj comp_id, uint32_t err_node, obj arg, bool *has_fattr)
{
	struct compiler_check_opts opts = {
		.mode = compiler_check_mode_compile,
		.comp_id = comp_id,
	};

	obj src = language_snippet_attribute(
		wk, get_obj_compiler(wk, comp_id)->lang, arg, compiler_check_snippet_kwargs(wk, &opts));

	if (!compiler_check(wk, &opts, src, err_node, has_fattr)) {
		return false;
	}

	compiler_check_log(wk, &opts, "has attribute %s: %s", get_cstr(wk, arg), bool_to_yn(*has_fattr));

	return true;
}

FUNC_IMPL(compiler, has_function_attribute, tc_bool, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs { kw_required };
	struct args_kw akw[] = { [kw_required] = { "required", tc_required_kw }, 0 };
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(requirement, kw_required);

	bool has_fattr;
	if (!compiler_has_function_attribute(wk, self, an[0].node, an[0].val, &has_fattr)) {
		return false;
	}

	compiler_handle_has_required_kw(requirement, has_fattr);

	*res = make_obj_bool(wk, has_fattr);
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

FUNC_IMPL(compiler, get_supported_function_attributes, tc_array, func_impl_flag_impure)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | obj_string }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = make_obj(wk, obj_array);

	return obj_array_foreach_flat(wk,
		an[0].val,
		&(struct func_compiler_get_supported_function_attributes_iter_ctx){
			.compiler = self,
			.arr = *res,
			.node = an[0].node,
		},
		func_compiler_get_supported_function_attributes_iter);
}

FUNC_IMPL(compiler, has_function, tc_bool, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compiler_check_mode_link,
	};

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_include_directories | cm_kw_required)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(requirement, cc_kw_required);

	obj src = language_snippet_has_function(
		wk, get_obj_compiler(wk, self)->lang, an[0].val, compiler_check_snippet_kwargs(wk, &opts));

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
		return false;
	}

	if (!ok) {
		obj src = language_snippet_has_builtin_function(
			wk, get_obj_compiler(wk, self)->lang, an[0].val, compiler_check_snippet_kwargs(wk, &opts));

		if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
			return false;
		}
	}

	compiler_handle_has_required_kw(requirement, ok);

	*res = make_obj_bool(wk, ok);

	compiler_check_log(wk, &opts, "has function %s: %s", get_cstr(wk, an[0].val), bool_to_yn(ok));

	return true;
}

FUNC_IMPL(compiler, has_header_symbol, tc_bool, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compiler_check_mode_compile,
	};

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_required | cm_kw_include_directories)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(required, cc_kw_required);

	bool ok;

	if (!compiler_check(wk,
		    &opts,
		    language_snippet_has_header_symbol(wk,
			    get_obj_compiler(wk, self)->lang,
			    an[0].val,
			    an[1].val,
			    compiler_check_snippet_kwargs(wk, &opts)),
		    an[0].node,
		    &ok)) {
		return false;
	}

	compiler_handle_has_required_kw(required, ok);

	*res = make_obj_bool(wk, ok);

	compiler_check_log(wk,
		&opts,
		"header %s has symbol %s: %s",
		get_cstr(wk, an[0].val),
		get_cstr(wk, an[1].val),
		bool_to_yn(ok));

	return true;
}

static bool
compiler_get_define(struct workspace *wk,
	uint32_t err_node,
	struct compiler_check_opts *opts,
	bool check_only,
	obj def,
	obj *res)
{
	opts->output_is_stdout = true;
	opts->keep_cmd_ctx = true;
	opts->mode = compiler_check_mode_preprocess;

	obj src = language_snippet_get_define(
		wk, get_obj_compiler(wk, opts->comp_id)->lang, def, compiler_check_snippet_kwargs(wk, opts));

	bool ok;
	if (!compiler_check(wk, opts, src, err_node, &ok)) {
		return false;
	} else if (!ok) {
		goto failed;
	}

	if (opts->from_cache) {
		*res = opts->cache_val;
		goto done;
	}

	const struct tstr *output = &opts->cmd_ctx.out;

	*res = 0;
	bool started = false;
	bool in_quotes = false;
	bool esc = false;
	bool joining = false;

	uint32_t i;
	for (i = 0; i < output->len; ++i) {
		struct str output_s = { &output->buf[i], output->len - i };

		if (!started) {
			// Look for the start delimiter.
			// The first character check is a minor optimization.
			if (output->buf[i] == '\"' && str_startswith(&output_s, &compiler_check_delimiter_start)) {
				started = true;
				*res = make_str(wk, "");
				i += compiler_check_delimiter_start.len;

				// Skip whitespace before the defined value or subsequent delimiter.
				for (; i < output->len && is_whitespace(output->buf[i]); ++i) {
				}

				// Replay the last character upon loop continue.
				--i;
			}
			continue;
		}

		// The start delimiter has been parsed at this point and we are building
		// the defined value `res` while looking for the end delimiter or line end.

		// Stop building defined value if we've encountered the end delimiter.
		// The first character check is a minor optimization.
		if (output->buf[i] == '\"' && str_startswith(&output_s, &compiler_check_delimiter_end)) {
			break;
		}

		switch (output->buf[i]) {
		case '"':
			if (esc) {
				esc = false;
			} else {
				in_quotes = !in_quotes;
				if (!in_quotes || joining) {
					uint32_t start = i;
					++i;
					for (; i < output->len; ++i) {
						if (!strchr("\t ", output->buf[i])) {
							break;
						}
					}

					if (output->buf[i] == '"') {
						joining = true;
						++i;
					} else {
						i = start;
					}
				}
			}
			break;
		case '\\': esc = true; break;
		}

		// Defined value ends upon line end.
		// We need to check both CR and LF.
		if (output->buf[i] == '\n' || output->buf[i] == '\r') {
			break;
		}

		// Append character to the defined value.
		str_appn(wk, res, &output->buf[i], 1);
	}

	if (*res && str_eql(get_str(wk, *res), &compiler_check_delimiter_sentinel)) {
		*res = 0;
	}

	run_cmd_ctx_destroy(&opts->cmd_ctx);

	compiler_check_cache_set(
		wk, opts->cache_key, &(struct compiler_check_cache_value){ .success = true, .value = *res });
done:
	if (check_only) {
		compiler_check_log(wk, opts, "defines %s %s", get_cstr(wk, def), bool_to_yn(!!*res));
	} else {
		if (!*res) {
			*res = make_str(wk, "");
		}

		compiler_check_log(wk, opts, "defines %s as '%s'", get_cstr(wk, def), get_cstr(wk, *res));
	}
	return true;
failed:
	vm_error_at(wk, err_node, "failed to %s define: '%s'", check_only ? "check" : "get", get_cstr(wk, def));
	return false;
}

FUNC_IMPL(compiler, get_define, tc_string, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;

	struct compiler_check_opts opts = { 0 };

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_include_directories)) {
		return false;
	}

	if (!compiler_get_define(wk, an[0].node, &opts, false, an[0].val, res)) {
		return false;
	}

	return true;
}

FUNC_IMPL(compiler, has_define, tc_bool, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;

	struct compiler_check_opts opts = { 0 };

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_include_directories | cm_kw_required)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(required, cc_kw_required);

	if (!compiler_get_define(
		    wk, an[0].node, &opts, true, an[0].val, res)) {
		return false;
	}

	compiler_handle_has_required_kw(required, !!*res);

	obj b;
	b = make_obj_bool(wk, !!*res);
	*res = b;

	return true;
}

FUNC_IMPL(compiler, symbols_have_underscore_prefix, tc_bool, func_impl_flag_impure)
{
	struct compiler_check_opts opts = { .comp_id = self };

	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	obj pre;
	if (!compiler_get_define(wk, 0, &opts, false, make_str(wk, "__USER_LABEL_PREFIX__"), &pre)) {
		return false;
	}

	*res = make_obj_bool(wk, str_eql(get_str(wk, pre), &STR("_")));
	return true;
}

static bool
func_compiler_check_common(struct workspace *wk, obj self, obj *res, enum compiler_check_mode mode)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = mode,
	};
	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_name | cm_kw_include_directories | cm_kw_werror
			    | cm_kw_required)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(requirement, cc_kw_required);

	enum obj_type t = get_obj_type(wk, an[0].val);

	obj src;

	switch (t) {
	case obj_string: src = an[0].val; break;
	case obj_file: {
		src = *get_obj_file(wk, an[0].val);
		opts.src_is_path = true;
		break;
	}
	default: vm_error_at(wk, an[0].node, "expected file or string, got %s", obj_type_to_s(t)); return false;
	}

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
		return false;
	}

	if (akw[cc_kw_name].set) {
		const char *mode_s = NULL;
		switch (mode) {
		case compiler_check_mode_run: mode_s = "runs"; break;
		case compiler_check_mode_link: mode_s = "links"; break;
		case compiler_check_mode_compile: mode_s = "compiles"; break;
		case compiler_check_mode_preprocess: mode_s = "preprocesses"; break;
		}

		compiler_check_log(wk, &opts, "%s %s: %s", get_cstr(wk, akw[cc_kw_name].val), mode_s, bool_to_yn(ok));
	}

	compiler_handle_has_required_kw(requirement, ok);

	*res = make_obj_bool(wk, ok);

	return true;
}

FUNC_IMPL(compiler, compiles, tc_bool, func_impl_flag_impure)
{
	return func_compiler_check_common(wk, self, res, compiler_check_mode_compile);
}

FUNC_IMPL(compiler, links, tc_bool, func_impl_flag_impure)
{
	return func_compiler_check_common(wk, self, res, compiler_check_mode_link);
}

static bool
compiler_check_header(struct workspace *wk,
	uint32_t err_node,
	struct compiler_check_opts *opts,
	obj hdr,
	enum requirement_type required,
	obj *res)
{
	obj src = language_snippet_check_header(
		wk, get_obj_compiler(wk, opts->comp_id)->lang, hdr, compiler_check_snippet_kwargs(wk, opts));

	bool ok;
	if (!compiler_check(wk, opts, src, err_node, &ok)) {
		return false;
	}

	const char *mode_s = NULL;
	switch (opts->mode) {
	case compiler_check_mode_compile: mode_s = "is usable"; break;
	case compiler_check_mode_preprocess: mode_s = "found"; break;
	default: abort();
	}

	compiler_handle_has_required_kw(required, ok);

	*res = make_obj_bool(wk, ok);

	compiler_check_log(wk, opts, "header %s %s: %s", get_cstr(wk, hdr), mode_s, bool_to_yn(ok));

	return true;
}

static bool
compiler_check_header_common(struct workspace *wk, obj self, obj *res, enum compiler_check_mode mode)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = mode,
		.output_is_stdout = mode == compiler_check_mode_preprocess,
		.dont_log_compiler_output = mode == compiler_check_mode_preprocess,
	};

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_required | cm_kw_include_directories)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(required, cc_kw_required);

	return compiler_check_header(wk, an[0].node, &opts, an[0].val, required, res);
}

FUNC_IMPL(compiler, has_header, tc_bool, func_impl_flag_impure)
{
	return compiler_check_header_common(wk, self, res, compiler_check_mode_preprocess);
}

FUNC_IMPL(compiler, check_header, tc_bool, func_impl_flag_impure)
{
	return compiler_check_header_common(wk, self, res, compiler_check_mode_compile);
}

FUNC_IMPL(compiler, has_type, tc_bool, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compiler_check_mode_compile,
	};

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_include_directories | cm_kw_required)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(required, cc_kw_required);

	obj src = language_snippet_has_type(
		wk, get_obj_compiler(wk, self)->lang, an[0].val, compiler_check_snippet_kwargs(wk, &opts));

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
		return false;
	}

	compiler_handle_has_required_kw(required, ok);

	*res = make_obj_bool(wk, ok);

	compiler_check_log(wk, &opts, "has type %s: %s", get_cstr(wk, an[0].val), bool_to_yn(ok));

	return true;
}

static bool
compiler_has_member(struct workspace *wk,
	struct compiler_check_opts *opts,
	uint32_t err_node,
	obj target,
	obj member,
	bool *res)
{
	opts->mode = compiler_check_mode_compile;

	obj src = language_snippet_has_member(
		wk, get_obj_compiler(wk, opts->comp_id)->lang, target, member, compiler_check_snippet_kwargs(wk, opts));

	if (!compiler_check(wk, opts, src, err_node, res)) {
		return false;
	}

	compiler_check_log(
		wk, opts, "struct %s has member %s: %s", get_cstr(wk, target), get_cstr(wk, member), bool_to_yn(*res));

	return true;
}

FUNC_IMPL(compiler, has_member, tc_bool, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, { obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = { 0 };

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_include_directories | cm_kw_required)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(required, cc_kw_required);

	bool ok;
	if (!compiler_has_member(wk, &opts, an[0].node, an[0].val, an[1].val, &ok)) {
		return false;
	}

	compiler_handle_has_required_kw(required, ok);

	*res = make_obj_bool(wk, ok);
	return true;
}

struct compiler_has_members_ctx {
	struct compiler_check_opts *opts;
	uint32_t node;
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
	if (!compiler_has_member(wk, ctx->opts, ctx->node, ctx->target, val, &ok)) {
		return ir_err;
	}

	if (!ok) {
		ctx->ok = false;
		return ir_done;
	}

	return ir_cont;
}

FUNC_IMPL(compiler, has_members, tc_bool, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, { TYPE_TAG_GLOB | obj_string }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = { 0 };

	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_prefix | cm_kw_include_directories | cm_kw_required)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(required, cc_kw_required);

	if (!get_obj_array(wk, an[1].val)->len) {
		vm_error_at(wk, an[1].node, "missing member arguments");
		return false;
	}

	struct compiler_has_members_ctx ctx = {
		.opts = &opts,
		.node = an[0].node,
		.target = an[0].val,
		.ok = true,
	};

	if (!obj_array_foreach_flat(wk, an[1].val, &ctx, compiler_has_members_iter)) {
		return false;
	}

	compiler_handle_has_required_kw(required, ctx.ok);

	*res = make_obj_bool(wk, ctx.ok);
	return true;
}

FUNC_IMPL(compiler, run, tc_run_result, func_impl_flag_impure)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	struct args_kw *akw;
	struct compiler_check_opts opts = {
		.mode = compiler_check_mode_run,
		.skip_run_check = true,
	};
	if (!func_compiler_check_args_common(wk,
		    self,
		    an,
		    &akw,
		    &opts,
		    cm_kw_args | cm_kw_dependencies | cm_kw_name | cm_kw_werror | cm_kw_required)) {
		return false;
	}

	obj o;
	if (!obj_array_flatten_one(wk, an[0].val, &o)) {
		vm_error_at(wk, an[0].node, "could not flatten argument");
	}

	enum obj_type t = get_obj_type(wk, an[0].val);

	obj src;

	switch (t) {
	case obj_string: src = an[0].val; break;
	case obj_file: {
		src = *get_obj_file(wk, an[0].val);
		opts.src_is_path = true;
		break;
	}
	default: vm_error_at(wk, an[0].node, "expected file or string, got %s", obj_type_to_s(t)); return false;
	}

	enum requirement_type requirement;
	{
		if (!akw[cc_kw_required].set) {
			requirement = requirement_auto;
		} else if (!coerce_requirement(wk, &akw[cc_kw_required], &requirement)) {
			return false;
		}
		if (requirement == requirement_skip) {
			*res = make_obj(wk, obj_run_result);
			struct obj_run_result *rr = get_obj_run_result(wk, *res);
			rr->flags |= run_result_flag_from_compile;
			return true;
		}
	}

	bool ok;
	if (!compiler_check(wk, &opts, src, an[0].node, &ok)) {
		return false;
	}

	if (akw[cc_kw_name].set) {
		compiler_check_log(wk, &opts, "runs %s: %s", get_cstr(wk, akw[cc_kw_name].val), bool_to_yn(ok));
	}

	compiler_handle_has_required_kw(requirement, ok);

	if (opts.from_cache) {
		*res = opts.cache_val;
	} else {
		*res = make_obj(wk, obj_run_result);
		struct obj_run_result *rr = get_obj_run_result(wk, *res);
		rr->flags |= run_result_flag_from_compile;

		if (ok) {
			rr->flags |= run_result_flag_compile_ok;
			rr->out = make_strn(wk, opts.cmd_ctx.out.buf, opts.cmd_ctx.out.len);
			rr->err = make_strn(wk, opts.cmd_ctx.err.buf, opts.cmd_ctx.err.len);
			rr->status = opts.cmd_ctx.status;
		}

		compiler_check_cache_set(
			wk, opts.cache_key, &(struct compiler_check_cache_value){ .success = ok, .value = *res });
		run_cmd_ctx_destroy(&opts.cmd_ctx);
	}

	return true;
}

static bool
compiler_warning_arg_needs_value_suffix(const struct str *arg_str)
{
	const struct str known[] = {
		STR("alloc-size-larger-than"),
		STR("alloca-larger-than"),
		STR("frame-larger-than"),
		STR("stack-usage"),
		STR("vla-larger-than"),
	};

	for (uint32_t i = 0; i < ARRAY_LEN(known); ++i) {
		if (str_eql(arg_str, &known[i])) {
			return true;
		}
	}
	return false;
}

static bool
compiler_has_argument(struct workspace *wk,
	obj comp_id,
	uint32_t err_node,
	obj arg,
	bool *has_argument,
	enum compiler_check_mode mode)
{
	obj args;
	args = make_obj(wk, obj_array);

	obj_array_extend(wk, args, toolchain_compiler_werror(wk, comp_id));

	if (get_obj_type(wk, arg) == obj_string) {
		// Convert "-Wno-" prefixes to the positive "-W" form in
		// order to get GCC to report the error.
		obj orig_arg = arg;
		struct str arg_str = *get_str(wk, arg);
		if (str_try_remove_prefix(&arg_str, &STR("-Wno-"))) {
			if (str_startswith(&arg_str, &STR("attributes"))) {
				// Exclude the special case "-Wno-attributes=...".
				// See: meson-tests/common/104 has arg/meson.build
			} else if (compiler_warning_arg_needs_value_suffix(&arg_str)) {
				arg = make_strf(wk, "-W%.*s=1000", arg_str.len, arg_str.s);
			} else {
				arg = make_strf(wk, "-W%.*s", arg_str.len, arg_str.s);
			}
		}

		obj_array_push(wk, args, arg);

		// Use the original arg form for user facing log messages
		arg = orig_arg;
	} else {
		obj_array_extend(wk, args, arg);

		obj str;
		obj_array_join(wk, true, arg, make_str(wk, " "), &str);
		arg = str;
	}

	struct compiler_check_opts opts = {
		.mode = mode,
		.comp_id = comp_id,
		.args = args,
		.keep_cmd_ctx = true,
	};

	obj src = language_snippet_minimal_exe(
		wk, get_obj_compiler(wk, comp_id)->lang, compiler_check_snippet_kwargs(wk, &opts));
	if (!compiler_check(wk, &opts, src, err_node, has_argument)) {
		return false;
	}

	if (!opts.from_cache) {
		bool option_ignored = false;
		struct tstr *outs[] = { &opts.cmd_ctx.out, &opts.cmd_ctx.err };
		for (uint32_t i = 0; i < ARRAY_LEN(outs); ++i) {
			if (!outs[i]->len) {
				continue;
			}

			if (toolchain_compiler_check_ignored_option(wk, comp_id, outs[i]->buf)) {
				option_ignored = true;
				break;
			} else if (toolchain_linker_check_ignored_option(wk, comp_id, outs[i]->buf)) {
				option_ignored = true;
				break;
			}
		}

		if (option_ignored) {
			*has_argument = false;

			compiler_check_cache_set(
				wk, opts.cache_key, &(struct compiler_check_cache_value){ .success = *has_argument });
		}

		run_cmd_ctx_destroy(&opts.cmd_ctx);
	}

	compiler_check_log(wk, &opts, "supports argument '%s': %s", get_cstr(wk, arg), bool_to_yn(*has_argument));

	return true;
}

struct func_compiler_get_supported_arguments_iter_ctx {
	uint32_t node;
	obj arr, compiler;
	enum compiler_check_mode mode;
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
compiler_has_argument_common(struct workspace *wk, obj self, type_tag glob, obj *res, enum compiler_check_mode mode)
{
	struct args_norm an[] = { { glob | obj_string }, ARG_TYPE_NULL };
	enum kwargs { kw_required };
	struct args_kw akw[] = { [kw_required] = { "required", tc_required_kw }, 0 };
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	compiler_handle_has_required_kw_setup(requirement, kw_required);

	bool has_argument;
	if (!compiler_has_argument(wk, self, an[0].node, an[0].val, &has_argument, mode)) {
		return false;
	}

	compiler_handle_has_required_kw(requirement, has_argument);

	*res = make_obj_bool(wk, has_argument);
	return true;
}

FUNC_IMPL(compiler, has_argument, tc_bool, func_impl_flag_impure)
{
	return compiler_has_argument_common(wk, self, 0, res, compiler_check_mode_compile);
}

FUNC_IMPL(compiler, has_link_argument, tc_bool, func_impl_flag_impure)
{
	return compiler_has_argument_common(wk, self, 0, res, compiler_check_mode_link);
}

FUNC_IMPL(compiler, has_multi_arguments, tc_bool, func_impl_flag_impure)
{
	return compiler_has_argument_common(wk, self, TYPE_TAG_GLOB, res, compiler_check_mode_compile);
}

FUNC_IMPL(compiler, has_multi_link_arguments, tc_bool, func_impl_flag_impure)
{
	return compiler_has_argument_common(wk, self, TYPE_TAG_GLOB, res, compiler_check_mode_link);
}

static bool
compiler_get_supported_arguments(struct workspace *wk, obj self, obj *res, enum compiler_check_mode mode)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | obj_string }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = make_obj(wk, obj_array);

	return obj_array_foreach_flat(wk,
		an[0].val,
		&(struct func_compiler_get_supported_arguments_iter_ctx){
			.compiler = self,
			.arr = *res,
			.node = an[0].node,
			.mode = mode,
		},
		func_compiler_get_supported_arguments_iter);
}

FUNC_IMPL(compiler, get_supported_arguments, tc_array, func_impl_flag_impure)
{
	return compiler_get_supported_arguments(wk, self, res, compiler_check_mode_compile);
}

FUNC_IMPL(compiler, get_supported_link_arguments, tc_array, func_impl_flag_impure)
{
	return compiler_get_supported_arguments(wk, self, res, compiler_check_mode_link);
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
		compiler_log(wk, ctx->compiler, "first supported argument: '%s'", get_cstr(wk, val_id));
		obj_array_push(wk, ctx->arr, val_id);
		return ir_done;
	}

	return ir_cont;
}

static bool
compiler_first_supported_argument(struct workspace *wk, obj self, obj *res, enum compiler_check_mode mode)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | obj_string }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = make_obj(wk, obj_array);

	return obj_array_foreach_flat(wk,
		an[0].val,
		&(struct func_compiler_get_supported_arguments_iter_ctx){
			.compiler = self,
			.arr = *res,
			.node = an[0].node,
			.mode = mode,
		},
		func_compiler_first_supported_argument_iter);
}

FUNC_IMPL(compiler, first_supported_argument, tc_array, func_impl_flag_impure)
{
	return compiler_first_supported_argument(wk, self, res, compiler_check_mode_compile);
}

FUNC_IMPL(compiler, first_supported_link_argument, tc_array, func_impl_flag_impure)
{
	return compiler_first_supported_argument(wk, self, res, compiler_check_mode_link);
}

FUNC_IMPL(compiler, get_id, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk,
		toolchain_component_type_to_id(wk,
			toolchain_component_compiler,
			get_obj_compiler(wk, self)->type[toolchain_component_compiler])
			->public_id);
	return true;
}

FUNC_IMPL(compiler, get_linker_id, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk,
		toolchain_component_type_to_id(wk,
			toolchain_component_linker,
			get_obj_compiler(wk, self)->type[toolchain_component_linker])
			->public_id);
	return true;
}

FUNC_IMPL(compiler, get_argument_syntax, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	const char *syntax = toolchain_compiler_flatten_one_optional(wk, toolchain_compiler_argument_syntax(wk, self));

	if (!syntax) {
		syntax = "other";
	}

	*res = make_str(wk, syntax);
	return true;
}

static obj
find_library_check_dirs(struct workspace *wk, const char *libname, obj libdirs, const char **exts, uint32_t exts_len)
{
	static const char *pref[] = { "", "lib" };

	TSTR(path);
	TSTR(lib);

	uint32_t i, j;
	obj libdir;
	obj_array_for(wk, libdirs, libdir) {
		for (i = 0; i < exts_len; ++i) {
			for (j = 0; j < ARRAY_LEN(pref); ++j) {
				tstr_clear(&lib);
				tstr_pushf(wk, &lib, "%s%s%s", pref[j], libname, exts[i]);

				path_join(wk, &path, get_cstr(wk, libdir), lib.buf);

				if (fs_file_exists(path.buf)) {
					return tstr_into_str(wk, &path);
				}
			}
		}
	}

	return 0;
}

struct find_library_result
find_library(struct workspace *wk, obj compiler, const char *libname, obj extra_dirs, enum find_library_flag flags)
{
	static const char *ext_order_static_preferred[] = { COMPILER_STATIC_LIB_EXTS, COMPILER_DYNAMIC_LIB_EXTS },
			  *ext_order_static_only[] = { COMPILER_STATIC_LIB_EXTS },
			  *ext_order_dynamic_preferred[] = { COMPILER_DYNAMIC_LIB_EXTS, COMPILER_STATIC_LIB_EXTS };

	const char **ext_order;
	uint32_t ext_order_len;

	if (flags & find_library_flag_only_static) {
		ext_order = ext_order_static_only;
		ext_order_len = ARRAY_LEN(ext_order_static_only);
	} else if (flags & find_library_flag_prefer_static) {
		ext_order = ext_order_static_preferred;
		ext_order_len = ARRAY_LEN(ext_order_static_preferred);
	} else {
		ext_order = ext_order_dynamic_preferred;
		ext_order_len = ARRAY_LEN(ext_order_dynamic_preferred);
	}

	obj found = 0;

	// First check in dirs if the kw is set.
	if (extra_dirs) {
		if ((found = find_library_check_dirs(wk, libname, extra_dirs, ext_order, ext_order_len))) {
			return (struct find_library_result){ found, find_library_found_location_extra_dirs };
		}
	}

	if (!compiler) {
		// If we don't have a compiler then just assume that -l $libname works
		return (struct find_library_result){ make_str(wk, libname), find_library_found_location_link_arg };
	}

	struct obj_compiler *comp = get_obj_compiler(wk, compiler);

	// Next, check system libdirs
	if (!found) {
		if ((found = find_library_check_dirs(wk, libname, comp->libdirs, ext_order, ext_order_len))) {
			return (struct find_library_result){ found, find_library_found_location_system_dirs };
		}
	}

	// Finally, just attempt to pass the libname on the linker command line
	if (!found) {
		bool ok = false;
		struct compiler_check_opts opts = { .mode = compiler_check_mode_link, .comp_id = compiler };
		opts.args = make_obj(wk, obj_array);
		obj_array_extend(wk, opts.args, toolchain_linker_lib(wk, compiler, libname));

		obj src = language_snippet_minimal_exe(
			wk, get_obj_compiler(wk, opts.comp_id)->lang, compiler_check_snippet_kwargs(wk, &opts));
		if (!compiler_check(wk, &opts, src, 0, &ok)) {
			return (struct find_library_result){ 0 };
		}

		if (ok) {
			return (struct find_library_result){ make_str(wk, libname),
				find_library_found_location_link_arg };
		}
	}

	return (struct find_library_result){ 0 };
}

void
find_library_result_to_dependency(struct workspace *wk, struct find_library_result find_result, obj compiler, obj d)
{
	struct obj_compiler *comp = get_obj_compiler(wk, compiler);
	struct obj_dependency *dep = get_obj_dependency(wk, d);
	dep->name = find_result.found;
	dep->type = dependency_type_external_library;
	dep->flags |= dep_flag_found;
	dep->machine = comp->machine;

	struct build_dep_raw raw = { 0 };

	if (find_result.location == find_library_found_location_link_arg) {
		raw.link_with_not_found = make_obj(wk, obj_array);
		obj_array_push(wk, raw.link_with_not_found, find_result.found);
	} else {
		raw.link_with = make_obj(wk, obj_array);
		obj_array_push(wk, raw.link_with, find_result.found);

		if (find_result.location == find_library_found_location_extra_dirs) {
			raw.build_rpath = make_obj(wk, obj_array);
			TSTR(dirname);
			path_dirname(wk, &dirname, get_cstr(wk, find_result.found));
			obj_array_push(wk, raw.build_rpath, tstr_into_str(wk, &dirname));
		}
	}

	if (!dependency_create(wk, &raw, &dep->dep, 0)) {
		UNREACHABLE;
	}

	dep->dep.link_language = comp->lang;
}

struct compiler_find_library_check_headers_ctx {
	uint32_t err_node;
	struct compiler_check_opts *opts;
	bool ok;
};

static enum iteration_result
compiler_find_library_check_headers_iter(struct workspace *wk, void *_ctx, obj hdr)
{
	struct compiler_find_library_check_headers_ctx *ctx = _ctx;

	obj res;
	if (!compiler_check_header(
		    wk, ctx->err_node, ctx->opts, hdr, requirement_auto, &res)) {
		return ir_err;
	}

	ctx->ok &= get_obj_bool(wk, res);
	return ir_cont;
}

FUNC_IMPL(compiler, find_library, tc_dependency, func_impl_flag_impure)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_static,
		kw_disabler,
		kw_dirs,

		// has_headers
		kw_has_headers,
		kw_header_required,
		kw_header_args,
		kw_header_dependencies,
		kw_header_include_directories,
		kw_header_no_builtin_args, // TODO
		kw_header_prefix,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required", tc_required_kw },
		[kw_static] = { "static", obj_bool },
		[kw_disabler] = { "disabler", obj_bool },
		[kw_dirs] = { "dirs", TYPE_TAG_LISTIFY | obj_string },
		// has_headers
		[kw_has_headers] = { "has_headers", TYPE_TAG_LISTIFY | obj_string },
		[kw_header_required] = { "header_required", obj_bool },
		[kw_header_args] = { "header_args", TYPE_TAG_LISTIFY | obj_string },
		[kw_header_dependencies] = { "header_dependencies", TYPE_TAG_LISTIFY | tc_dependency },
		[kw_header_include_directories] = { "header_include_directories", TYPE_TAG_LISTIFY | tc_coercible_inc },
		[kw_header_no_builtin_args] = { "header_no_builtin_args", },
		[kw_header_prefix] = { "header_prefix", },
		0
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	*res = make_obj(wk, obj_dependency);
	struct obj_dependency *dep = get_obj_dependency(wk, *res);
	dep->type = dependency_type_external_library;

	// Validate args
	if (!akw[kw_has_headers].set) {
		uint32_t i;
		for (i = kw_header_required; i <= kw_header_prefix; ++i) {
			if (akw[i].set) {
				vm_error_at(wk,
					akw[i].node,
					"header_ keywords are invalid without "
					"also specifying the has_headers keyword");
				return false;
			}
		}
	}

	enum requirement_type requirement;
	{ // Handle disabled libs
		if (!coerce_requirement(wk, &akw[kw_required], &requirement)) {
			return false;
		}

		if (requirement == requirement_skip) {
			return true;
		}
	}

	// Determine requested lib type
	enum find_library_flag flags = 0;
	{
		if (!akw[kw_static].set) {
			get_option_value(wk, current_project(wk), "prefer_static", &akw[kw_static].val);
		}

		if (get_obj_bool(wk, akw[kw_static].val)) {
			flags |= find_library_flag_only_static;
		}
	}

	struct find_library_result find_result
		= find_library(wk, self, get_cstr(wk, an[0].val), akw[kw_dirs].val, flags);
	bool found = find_result.found != 0;

	if (found && akw[kw_has_headers].set) {
		struct compiler_check_opts opts = { 0 };
		struct args_kw header_kwargs[cc_kwargs_count + 1] = {
			[cc_kw_args] = akw[kw_header_args],
			[cc_kw_dependencies] = akw[kw_header_dependencies],
			[cc_kw_prefix] = akw[kw_header_prefix],
			[cc_kw_required] = akw[kw_header_required],
			[cc_kw_include_directories] = akw[kw_header_include_directories],
		};
		compiler_opts_init(self, header_kwargs, &opts);

		struct compiler_find_library_check_headers_ctx check_headers_ctx = {
			.ok = true,
			.err_node = akw[kw_has_headers].node,
			.opts = &opts,
		};
		obj_array_foreach(
			wk, akw[kw_has_headers].val, &check_headers_ctx, compiler_find_library_check_headers_iter);

		if (!check_headers_ctx.ok) {
			found = false;
		}
	}

	compiler_log(wk, self, "library %s found: %s", get_cstr(wk, an[0].val), bool_to_yn(found));

	if (found) {
		obj_lprintf(wk, log_debug, "library resolved to %#o\n", find_result.found);
		find_library_result_to_dependency(wk, find_result, self, *res);
	} else {
		if (requirement == requirement_required) {
			vm_error_at(wk, an[0].node, "required library not found");
			return false;
		}

		if (akw[kw_disabler].set && get_obj_bool(wk, akw[kw_disabler].val)) {
			*res = obj_disabler;
		}
	}

	return true;
}

FUNC_IMPL(compiler, preprocess, tc_array, func_impl_flag_impure)
{
	struct args_norm an[]
		= { { TYPE_TAG_GLOB | tc_string | tc_file | tc_custom_target | tc_generated_list }, ARG_TYPE_NULL };
	enum kwargs {
		kw_compile_args,
		kw_include_directories,
		kw_output,
		kw_dependencies,
		kw_depends,
	};
	struct args_kw akw[] = {
		[kw_compile_args] = { "compile_args", TYPE_TAG_LISTIFY | tc_string },
		[kw_include_directories] = { "include_directories", TYPE_TAG_LISTIFY | tc_coercible_inc },
		[kw_output] = { "output", tc_string },
		[kw_dependencies] = { "dependencies", TYPE_TAG_LISTIFY | tc_dependency },
		[kw_depends] = { "depends", tc_depends_kw },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	struct obj_compiler *comp = get_obj_compiler(wk, self);

	obj depends = 0;
	if (akw[kw_depends].set) {
		if (!coerce_files(wk, akw[kw_depends].node, akw[kw_depends].val, &depends)) {
			return false;
		}
	}

	obj base_cmd;
	obj_array_dup(wk, comp->cmd_arr[toolchain_component_compiler], &base_cmd);

	obj_array_extend(wk, base_cmd, toolchain_compiler_preprocess_only(wk, self));

	obj_array_extend(
		wk, base_cmd, toolchain_compiler_force_language(wk, self, compiler_language_to_s(wk, comp->lang)));

	ca_get_std_args(wk, self, current_project(wk), NULL, base_cmd);

	ca_get_option_compile_args(wk, comp->lang, current_project(wk), NULL, base_cmd);

	bool have_dep = false;
	struct build_dep dep = { 0 };
	if (akw[kw_dependencies].set) {
		have_dep = true;
		dep_process_deps(wk, akw[kw_dependencies].val, &dep);
		obj_array_extend_nodup(wk, base_cmd, dep.compile_args);
	}

	obj_array_extend(wk, base_cmd, toolchain_compiler_include(wk, self, "@OUTDIR@"));
	obj_array_extend(wk, base_cmd, toolchain_compiler_include(wk, self, "@CURRENT_SOURCE_DIR@"));

	if (!add_include_directory_args(wk, &akw[kw_include_directories], have_dep ? &dep : 0, self, base_cmd)) {
		return false;
	}

	if (akw[kw_compile_args].set) {
		obj_array_extend(wk, base_cmd, akw[kw_compile_args].val);
	}

	*res = make_obj(wk, obj_array);

	TSTR(output_dir);
	tstr_pushs(wk, &output_dir, get_cstr(wk, current_project(wk)->build_dir));
	path_push(wk, &output_dir, "preprocess.p");

	if (!fs_mkdir_p(wk, output_dir.buf)) {
		return false;
	}

	obj output;
	if (akw[kw_output].set) {
		output = akw[kw_output].val;
	} else {
		output = make_str(wk, "@PLAINNAME@.i");
	}

	obj v;
	obj_array_for(wk, an[0].val, v) {
		obj cmd;
		obj_array_dup(wk, base_cmd, &cmd);

		obj_array_push(wk, cmd, make_str(wk, "@INPUT@"));

		struct make_custom_target_opts opts = {
			.input_node = an[0].node,
			.input_orig = v,
			.output_orig = output,
			.output_dir = output_dir.buf,
			.command_orig = cmd,
			.extra_args_valid = true,
			.capture = true,
		};

		obj tgt;
		if (!make_custom_target(wk, &opts, &tgt)) {
			return false;
		}

		struct obj_custom_target *t = get_obj_custom_target(wk, tgt);

		obj output;
		if (!obj_array_flatten_one(wk, t->output, &output)) {
			UNREACHABLE;
		}

		t->name = make_strf(wk, "<preprocess:%s>", get_file_path(wk, output));
		if (depends) {
			obj_array_extend_nodup(wk, t->depends, depends);
		}

		obj_array_push(wk, current_project(wk)->targets, tgt);

		obj_array_push(wk, *res, output);
	}

	return true;
}

FUNC_IMPL(compiler, cmd_array, tc_array, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_compiler(wk, self)->cmd_arr[toolchain_component_compiler];
	return true;
}

FUNC_IMPL(compiler, version, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_compiler(wk, self)->ver[toolchain_component_compiler];
	return true;
}

FUNC_IMPL(compiler, configure, 0, func_impl_flag_impure)
{
	struct args_norm an[] = { { complex_type_enum_get(wk, enum toolchain_component) }, ARG_TYPE_NULL };
	enum kwargs {
		kw_overwrite,
		kw_cmd_array,
		kw_handlers,
		kw_libdirs,
		kw_version,
	};
	struct args_kw akw[] = {
		[kw_overwrite] = { "overwrite", tc_bool },
		[kw_cmd_array] = { "cmd_array", TYPE_TAG_LISTIFY | tc_string },
		[kw_handlers] = { "handlers", COMPLEX_TYPE_PRESET(tc_cx_toolchain_overrides) },
		[kw_libdirs] = { "libdirs", TYPE_TAG_LISTIFY | tc_string },
		[kw_version] = { "version", tc_string },
		0,
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	uint32_t component;
	if (!toolchain_component_from_s(wk, get_cstr(wk, an[0].val), &component)) {
		vm_error(wk, "unknown toolchain component %o", an[0].val);
		return false;
	}

	struct obj_compiler *c = get_obj_compiler(wk, self);

	if (akw[kw_handlers].set) {
		if (!toolchain_overrides_validate(wk, akw[kw_handlers].node, akw[kw_handlers].val, component)) {
			return false;
		}

		obj *overrides = &c->overrides[component];

		bool overwrite = akw[kw_overwrite].set ? get_obj_bool(wk, akw[kw_overwrite].val) : *overrides == 0;

		if (overwrite) {
			*overrides = akw[kw_handlers].val;
		} else if (!*overrides) {
			vm_error(wk, "unable to merge overrides: there are no existing overrides");
			return false;
		} else {
			obj_dict_merge_nodup(wk, *overrides, akw[kw_handlers].val);
		}
	}

	if (akw[kw_cmd_array].set) {
		c->cmd_arr[component] = akw[kw_cmd_array].val;
	}

	if (akw[kw_libdirs].set) {
		if (component != toolchain_component_compiler) {
			vm_error(wk, "libdirs only configurable for compiler");
			return false;
		}

		c->libdirs = akw[kw_libdirs].val;
	}

	if (akw[kw_version].set) {
		c->ver[component] = akw[kw_version].val;
	}

	return true;
}

FUNC_IMPL(compiler, get_internal_id, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk,
		toolchain_component_type_to_id(wk,
			toolchain_component_compiler,
			get_obj_compiler(wk, self)->type[toolchain_component_compiler])
			->id);
	return true;
}

FUNC_IMPL(compiler, get_internal_linker_id, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk,
		toolchain_component_type_to_id(
			wk, toolchain_component_linker, get_obj_compiler(wk, self)->type[toolchain_component_linker])
			->id);
	return true;
}

FUNC_IMPL(compiler, get_internal_archiver_id, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_str(wk,
		toolchain_component_type_to_id(wk,
			toolchain_component_archiver,
			get_obj_compiler(wk, self)->type[toolchain_component_archiver])
			->id);
	return true;
}


FUNC_IMPL(compiler, target_triple, tc_dict, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = machine_parsed_triple_to_obj(wk, &get_obj_compiler(wk, self)->triple);
	return true;
}

FUNC_IMPL(compiler, machine, tc_machine, func_impl_flag_impure, .desc = "Get the machine this compiler object is for")
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	const char *machine_var = 0;
	switch (get_obj_compiler(wk, self)->machine) {
	case machine_kind_build: machine_var = "build_machine"; break;
	case machine_kind_host: machine_var = "host_machine"; break;
	case machine_kind_either: machine_var = 0; break;
	}

	if (!machine_var) {
		vm_error(wk, "this copmiler has no concrete machine defined");
		return false;
	}


	if (!obj_dict_index_str(wk, wk->vm.default_global_scope, machine_var, res)) {
		UNREACHABLE;
	}

	return true;
}

FUNC_IMPL(compiler,
	native,
	tc_bool,
	func_impl_flag_impure,
	.desc = "Return true if this compiler object is for"
		" the build machine, or false if it is for the host machine.")
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	switch (get_obj_compiler(wk, self)->machine) {
	case machine_kind_build: *res = obj_bool_true; break;
	case machine_kind_host: *res = obj_bool_false; break;
	default: vm_error(wk, "this copmiler has no concrete machine defined"); return false;
	}

	return true;
}

FUNC_IMPL(compiler, version_raw, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = get_obj_compiler(wk, self)->ver_raw;
	return true;
}

FUNC_REGISTER(compiler)
{
	FUNC_IMPL_REGISTER(compiler, alignment);
	FUNC_IMPL_REGISTER(compiler, check_header);
	FUNC_IMPL_REGISTER(compiler, cmd_array);
	FUNC_IMPL_REGISTER(compiler, compiles);
	FUNC_IMPL_REGISTER(compiler, compute_int);
	FUNC_IMPL_REGISTER(compiler, find_library);
	FUNC_IMPL_REGISTER(compiler, first_supported_argument);
	FUNC_IMPL_REGISTER(compiler, first_supported_link_argument);
	FUNC_IMPL_REGISTER(compiler, get_argument_syntax);
	FUNC_IMPL_REGISTER(compiler, get_define);
	FUNC_IMPL_REGISTER(compiler, get_id);
	FUNC_IMPL_REGISTER(compiler, get_linker_id);
	FUNC_IMPL_REGISTER(compiler, get_supported_arguments);
	FUNC_IMPL_REGISTER(compiler, get_supported_function_attributes);
	FUNC_IMPL_REGISTER(compiler, get_supported_link_arguments);
	FUNC_IMPL_REGISTER(compiler, has_argument);
	FUNC_IMPL_REGISTER(compiler, has_define);
	FUNC_IMPL_REGISTER(compiler, has_function);
	FUNC_IMPL_REGISTER(compiler, has_function_attribute);
	FUNC_IMPL_REGISTER(compiler, has_header);
	FUNC_IMPL_REGISTER(compiler, has_header_symbol);
	FUNC_IMPL_REGISTER(compiler, has_link_argument);
	FUNC_IMPL_REGISTER(compiler, has_member);
	FUNC_IMPL_REGISTER(compiler, has_members);
	FUNC_IMPL_REGISTER(compiler, has_multi_arguments);
	FUNC_IMPL_REGISTER(compiler, has_multi_link_arguments);
	FUNC_IMPL_REGISTER(compiler, has_type);
	FUNC_IMPL_REGISTER(compiler, links);
	FUNC_IMPL_REGISTER(compiler, preprocess);
	FUNC_IMPL_REGISTER(compiler, run);
	FUNC_IMPL_REGISTER(compiler, sizeof);
	FUNC_IMPL_REGISTER(compiler, symbols_have_underscore_prefix);
	FUNC_IMPL_REGISTER(compiler, version);

	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(compiler, configure);
		FUNC_IMPL_REGISTER(compiler, get_internal_id);
		FUNC_IMPL_REGISTER(compiler, get_internal_linker_id);
		FUNC_IMPL_REGISTER(compiler, get_internal_archiver_id);
		FUNC_IMPL_REGISTER(compiler, target_triple);
		FUNC_IMPL_REGISTER(compiler, machine);
		FUNC_IMPL_REGISTER(compiler, native);
		FUNC_IMPL_REGISTER(compiler, version_raw);
	}
}
