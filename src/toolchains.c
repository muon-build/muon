/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-FileCopyrightText: Owen Rafferty <owen@owenrafferty.com>
 * SPDX-FileCopyrightText: illiliti <illiliti@thunix.net>
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "error.h"
#include "functions/kernel.h"
#include "functions/string.h"
#include "guess.h"
#include "lang/analyze.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "machines.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "sha_256.h"
#include "toolchains.h"

obj
compiler_check_cache_key(struct workspace *wk, const struct compiler_check_cache_key *key)
{
	uint32_t argstr_len;
	{
		uint32_t i = 0;
		const char *p = key->argstr;
		for (;; ++p) {
			if (!p[0]) {
				if (++i >= key->argc) {
					break;
				}
			}
		}

		argstr_len = p - key->argstr;
	}

	enum {
		sha_idx_argstr = 0,
		sha_idx_ver = sha_idx_argstr + 32,
		sha_idx_src = sha_idx_ver + 32,
		sha_data_len = sha_idx_src + 32
	};

	uint8_t sha_data[sha_data_len] = { 0 };

	calc_sha_256(&sha_data[sha_idx_argstr], key->argstr, argstr_len);
	if (key->comp && key->comp->ver[toolchain_component_compiler]) {
		const struct str *ver = get_str(wk, key->comp->ver[toolchain_component_compiler]);
		calc_sha_256(&sha_data[sha_idx_ver], ver->s, ver->len);
	}
	if (key->src) {
		calc_sha_256(&sha_data[sha_idx_src], key->src, strlen(key->src));
	}

	uint8_t sha[32];
	calc_sha_256(sha, sha_data, sha_data_len);

	/* LLOG_I("sha: "); */
	/* uint32_t i; */
	/* for (i = 0; i < 32; ++i) { */
	/* 	log_plain("%02x", sha_res[i]); */
	/* } */
	/* log_plain("\n"); */

	return make_strn(wk, (const char *)sha, 32);
}

bool
compiler_check_cache_get(struct workspace *wk, obj key, struct compiler_check_cache_value *val)
{
	obj arr;
	if (obj_dict_index(wk, wk->compiler_check_cache, key, &arr)) {
		obj cache_res;
		cache_res = obj_array_index(wk, arr, 0);
		val->success = get_obj_bool(wk, cache_res);
		val->value = obj_array_index(wk, arr, 1);
		return true;
	} else {
		return false;
	}
}

void
compiler_check_cache_set(struct workspace *wk, obj key, const struct compiler_check_cache_value *val)
{
	if (!key) {
		return;
	}

	obj arr;
	if (obj_dict_index(wk, wk->compiler_check_cache, key, &arr)) {
		obj_array_set(wk, arr, 0, make_obj_bool(wk, val->success));
		obj_array_set(wk, arr, 1, val->value);
	} else {
		arr = make_obj(wk, obj_array);

		obj_array_push(wk, arr, make_obj_bool(wk, val->success));
		obj_array_push(wk, arr, val->value);

		obj_dict_set(wk, wk->compiler_check_cache, key, arr);
	}
}

bool
toolchain_component_type_from_s(struct workspace *wk, enum toolchain_component comp, const char *name, uint32_t *res)
{
	obj o;
	if (obj_dict_index_str(wk, wk->toolchain_registry.ids[comp], name, &o)) {
		*res = get_obj_number(wk, o);
		return true;
	}
	return false;
}

const struct toolchain_id *
toolchain_component_type_to_id(struct workspace *wk, enum toolchain_component comp, uint32_t val)
{
	return &((struct toolchain_registry_component *)arr_get(&wk->toolchain_registry.components[comp], val))->id;
}

static const struct toolchain_id toolchain_component_name[] = {
	[toolchain_component_compiler] = { "compiler" },
	[toolchain_component_linker] = { "linker" },
	[toolchain_component_static_linker] = { "static_linker" },
};

const char *
toolchain_component_to_s(enum toolchain_component comp)
{
	return toolchain_component_name[comp].id;
}

bool
toolchain_component_from_s(const char *name, uint32_t *res)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(toolchain_component_name); ++i) {
		if (strcmp(name, toolchain_component_name[i].id) == 0) {
			*res = i;
			return true;
		}
	}

	return false;
}

static const char *compiler_language_names[compiler_language_count] = {
	[compiler_language_null] = "null",
	[compiler_language_c] = "c",
	[compiler_language_c_hdr] = "c_hdr",
	[compiler_language_cpp] = "cpp",
	[compiler_language_cpp_hdr] = "cpp_hdr",
	[compiler_language_objc_hdr] = "objc_hdr",
	[compiler_language_objcpp_hdr] = "objcpp_hdr",
	[compiler_language_c_obj] = "c_obj",
	[compiler_language_objc] = "objc",
	[compiler_language_objcpp] = "objcpp",
	[compiler_language_assembly] = "assembly",
	[compiler_language_llvm_ir] = "llvm_ir",
	[compiler_language_nasm] = "nasm",
};

static const char *compiler_language_gcc_names[compiler_language_count] = {
	[compiler_language_c] = "c",
	[compiler_language_c_hdr] = "c-header",
	[compiler_language_cpp] = "c++",
	[compiler_language_cpp_hdr] = "c++-header",
	[compiler_language_objc] = "objective-c",
	[compiler_language_objc_hdr] = "objective-c-header",
	[compiler_language_objcpp] = "objective-c++",
	[compiler_language_objcpp_hdr] = "objective-c++-header",
};

const char *
compiler_language_to_s(enum compiler_language l)
{
	assert(l < compiler_language_count);
	return compiler_language_names[l];
}

const char *
compiler_language_to_gcc_name(enum compiler_language l)
{
	const char *n = compiler_language_gcc_names[l];
	assert(n);
	return n;
}

bool
s_to_compiler_language(const char *_s, enum compiler_language *l)
{
	const struct str s = STRL(_s);
	uint32_t i;
	for (i = 0; i < compiler_language_count; ++i) {
		if (str_eqli(&s, &STRL(compiler_language_names[i]))) {
			*l = i;
			return true;
		}
	}

	return false;
}

enum compiler_language
compiler_language_to_hdr(enum compiler_language lang)
{
	switch (lang) {
	case compiler_language_c: return compiler_language_c_hdr;
	case compiler_language_cpp: return compiler_language_cpp_hdr;
	case compiler_language_objc: return compiler_language_objc_hdr;
	case compiler_language_objcpp: return compiler_language_objcpp_hdr;
	default: UNREACHABLE_RETURN;
	}
}

static const char *compiler_language_exts[compiler_language_count][10] = {
	[compiler_language_c] = { "c" },
	[compiler_language_c_hdr] = { "h" },
	[compiler_language_cpp] = { "cc", "cpp", "cxx", "C" },
	[compiler_language_cpp_hdr] = { "hh", "hpp", "hxx" },
	[compiler_language_c_obj] = { "o", "obj" },
	[compiler_language_objc] = { "m", "M" },
	[compiler_language_objcpp] = { "mm" },
	[compiler_language_assembly] = { "S", "s" },
	[compiler_language_llvm_ir] = { "ll" },
	[compiler_language_nasm] = { "asm" },
};

bool
filename_to_compiler_language(const char *str, enum compiler_language *l)
{
	uint32_t i, j;
	const char *ext;

	if (!(ext = strrchr(str, '.'))) {
		return false;
	}
	++ext;

	for (i = 0; i < compiler_language_count; ++i) {
		for (j = 0; compiler_language_exts[i][j]; ++j) {
			if (strcmp(ext, compiler_language_exts[i][j]) == 0) {
				*l = i;
				return true;
			}
		}
	}

	return false;
}

const char *
compiler_language_extension(enum compiler_language l)
{
	return compiler_language_exts[l][0];
}

enum compiler_language
coalesce_link_languages(enum compiler_language cur, enum compiler_language new)
{
	switch (new) {
	case compiler_language_null:
	case compiler_language_c_hdr:
	case compiler_language_cpp_hdr:
	case compiler_language_objc_hdr:
	case compiler_language_objcpp_hdr:
	case compiler_language_llvm_ir: break;
	case compiler_language_assembly:
		if (!cur) {
			return compiler_language_assembly;
		}
		break;
	case compiler_language_nasm:
	case compiler_language_c:
	case compiler_language_c_obj:
	case compiler_language_objc:
		if (!cur) {
			return compiler_language_c;
		}
		break;
	case compiler_language_cpp:
	case compiler_language_objcpp:
		if (!cur || cur == compiler_language_c || cur == compiler_language_assembly) {
			return compiler_language_cpp;
		}
		break;
	case compiler_language_count: UNREACHABLE;
	}

	return cur;
}

static bool
run_cmd_arr(struct workspace *wk, struct run_cmd_ctx *cmd_ctx, obj cmd_arr, const char *arg)
{
	obj args = cmd_arr;
	if (arg) {
		obj_array_dup(wk, cmd_arr, &args);
		obj_array_push(wk, args, make_str(wk, arg));
	}

	const char *argstr;
	uint32_t argc;
	join_args_argstr(wk, &argstr, &argc, args);

	obj cache_key = compiler_check_cache_key(wk,
		&(struct compiler_check_cache_key){
			.argstr = argstr,
			.argc = argc,
		});

	struct compiler_check_cache_value cache_val = { 0 };
	if (compiler_check_cache_get(wk, cache_key, &cache_val)) {
		if (!cache_val.success) {
			return false;
		}

		obj status, err, out;
		status = obj_array_index(wk, cache_val.value, 0);
		out = obj_array_index(wk, cache_val.value, 1);
		err = obj_array_index(wk, cache_val.value, 2);

		cmd_ctx->status = get_obj_number(wk, status);

		const struct str *err_str = get_str(wk, err), *out_str = get_str(wk, out);

		cmd_ctx->out = (struct tstr){
			.buf = (char *)out_str->s,
			.len = out_str->len,
			.cap = out_str->len,
			.s = out,
		};

		cmd_ctx->err = (struct tstr){
			.buf = (char *)err_str->s,
			.len = err_str->len,
			.cap = err_str->len,
			.s = err,
		};

		return true;
	}

	bool success = true;

	if (!run_cmd(wk, cmd_ctx, argstr, argc, NULL, 0)) {
		L("failed to run command %s", argstr);
		run_cmd_print_error(cmd_ctx, log_debug);

		run_cmd_ctx_destroy(cmd_ctx);
		return false;
	}

	cache_val.success = success;
	cache_val.value = make_obj(wk, obj_array);

	obj status;
	status = make_obj(wk, obj_number);
	set_obj_number(wk, status, cmd_ctx->status);
	obj_array_push(wk, cache_val.value, status);
	obj_array_push(wk, cache_val.value, make_strn(wk, cmd_ctx->out.buf, cmd_ctx->out.len));
	obj_array_push(wk, cache_val.value, make_strn(wk, cmd_ctx->err.buf, cmd_ctx->err.len));
	compiler_check_cache_set(wk, cache_key, &cache_val);

	return success;
}

static bool
run_cmd_args(struct workspace *wk, struct run_cmd_ctx *cmd_ctx, obj cmd_arr, const struct args *args)
{
	obj cmd_arr_to_run;
	obj_array_dup(wk, cmd_arr, &cmd_arr_to_run);
	push_args(wk, cmd_arr_to_run, args);
	return run_cmd_arr(wk, cmd_ctx, cmd_arr_to_run, 0);
}

struct toolchain_exe_detect_candidate {
	int64_t score;
	uint32_t idx;
	obj output;
	obj cmd_arr;
	obj overrides;
	bool found;
};

static bool
toolchain_component_detect_check_list(struct workspace *wk,
	enum toolchain_component component,
	obj list,
	obj output,
	obj cmd_arr,
	struct toolchain_exe_detect_candidate *candidate)
{
	const struct arr *registry = &wk->toolchain_registry.components[component];
	const struct args_norm detect_an[] = { { .val = output }, { ARG_TYPE_NULL } };

	obj idx;
	obj_array_for(wk, list, idx) {
		const struct toolchain_registry_component *rc = arr_get(registry, idx);

		int64_t score = 1;

		if (rc->detect) {
			obj res;
			if (!vm_eval_capture(wk, rc->detect, detect_an, 0, &res)) {
				return false;
			}

			score = get_obj_number(wk, res);
		}

		if (score > candidate->score) {
			L("%s new high score: %" PRId64, rc->id.id, score);
			candidate->score = score;
			candidate->idx = idx;
			candidate->found = true;
			candidate->output = output;
			candidate->cmd_arr = cmd_arr;
			candidate->overrides = rc->overrides;
		}
	}

	return true;
}

static void
toolchain_component_detect_apply_candidate(struct obj_compiler *compiler,
	enum toolchain_component component,
	const struct toolchain_exe_detect_candidate *candidate,
	obj ver,
	bool *found)
{
	compiler->cmd_arr[component] = candidate->cmd_arr;
	compiler->type[component] = candidate->idx;
	compiler->ver[component] = ver;
	compiler->overrides[component] = candidate->overrides;
	if (component == toolchain_component_compiler) {
		compiler->ver_raw = candidate->output;
	}
	*found = true;
}

static bool
toolchain_component_detect(struct workspace *wk,
	enum toolchain_component component,
	obj comp,
	bool *found)
{
	L("detecting component %s", toolchain_component_to_s(component));

	struct obj_compiler *compiler = get_obj_compiler(wk, comp);
	const struct arr *registry = &wk->toolchain_registry.components[component];

	*found = false;

	// determine candidate executable list

	obj *candidates = 0;
	uint32_t candidates_len = 0;
	uint32_t preferred_type = 0; // TODO: use this do assist in detection of linker / static_linker

	// first check if the option for this component has been set, e.g. env.CC
	{
		TSTR(opt_name);
		if (toolchain_component_option_name(wk, compiler->lang, component, compiler->machine, &opt_name)) {
			obj cmd_arr_opt = 0;
			if (!get_option(wk, NULL, &TSTR_STR(&opt_name), &cmd_arr_opt)) {
				UNREACHABLE;
			}
			struct obj_option *cmd_arr = get_obj_option(wk, cmd_arr_opt);

			if (cmd_arr->source > option_value_source_default) {
				candidates = ar_maken(wk->a_scratch, obj, 1);
				candidates_len = 1;
				candidates[0] = cmd_arr->val;
			}
		}
	}

	// if no option was set, construct a list of candidate executables
	bool do_linker_passthrough = false;
	if (!candidates) {
		if (component == toolchain_component_compiler) {
			// for the compiler component, construct a bootstrap exe list based
			// on all the registered component's exe fields
			obj exe_list = make_obj(wk, obj_dict);
			for (uint32_t i = 1 /* skip the empty toolchain */; i < registry->len; ++i) {
				const struct toolchain_registry_component *base = arr_get(registry, i);

				obj exe;
				if (!obj_dict_geti(wk, base->exe, compiler->lang, &exe)) {
					continue;
				}

				obj_dict_set(wk, exe_list, exe, obj_bool_true);
			}
			candidates_len = get_obj_dict(wk, exe_list)->len;

			if (!candidates_len) {
				LOG_E("no candidate executables defined for language %s", compiler_language_to_s(compiler->lang));
				return false;
			}

			candidates = ar_maken(wk->a_scratch, obj, candidates_len);
			uint32_t i = 0;
			obj exe, _;
			obj_dict_for(wk, exe_list, exe, _) {
				(void)_;
				candidates[i] = make_obj(wk, obj_array);
				obj_array_push(wk, candidates[i], exe);
				++i;
			}
		} else {
			// for the linker components invoke the compiler's logic to decide
			// what candidate to use
			candidates = ar_maken(wk->a_scratch, obj, 1);
			candidates_len = 1;

			const struct toolchain_registry_component *rc
				= arr_get(&wk->toolchain_registry.components[toolchain_component_compiler],
					compiler->type[toolchain_component_compiler]);

			if (rc->sub_components[component].fn) {
				obj res;

				struct args_norm an[] = { { .val = comp }, { ARG_TYPE_NULL } };
				if (!vm_eval_capture(wk, rc->sub_components[component].fn, an, 0, &res)) {
					return false;
				}

				if (!toolchain_component_type_from_s(wk, component, get_cstr(wk, res), &preferred_type)) {
					vm_error_at(wk,
						get_obj_capture(wk, rc->sub_components[component].fn)->func->entry,
						"unknown %s type returned from toolchain function",
						toolchain_component_to_s(component));
					return false;
				}
			} else if (rc->sub_components[component].type) {
				preferred_type = rc->sub_components[component].type;
			} else {
				L("skipping %s detecting for %s compiler",
					toolchain_component_to_s(component),
					compiler_language_to_s(compiler->lang));
				*found = true;
				return true;
			}

			if (component == toolchain_component_linker && toolchain_compiler_do_linker_passthrough(wk, comp)) {
				do_linker_passthrough = true;
				candidates[0] = compiler->cmd_arr[toolchain_component_compiler];
			} else {
				const struct toolchain_registry_component *sub_component = arr_get(registry, preferred_type);
				candidates[0] = make_obj(wk, obj_array);
				obj_array_push(wk, candidates[0], sub_component->exe);
			}
		}
	}

	// now determine lists of toolchains to check against, grouped by common
	// version argument.  The default_list is composed of toolchains with no
	// version argument defined.
	obj toolchains_grouped_by_version_arg = make_obj(wk, obj_dict);
	obj toolchains_with_no_version_arg = make_obj(wk, obj_array);
	{
		for (uint32_t i = 1 /* skip the empty toolchain */; i < registry->len; ++i) {
			const struct args *args = 0;
			const struct toolchain_registry_component *base = arr_get(registry, i);

			if (preferred_type && i != preferred_type) {
				continue;
			} else if (component == toolchain_component_compiler) {
				obj _v;
				if (!obj_dict_geti(wk, base->exe, compiler->lang, &_v)) {
					continue;
				}
			}

			compiler->type[component] = i;
			compiler->overrides[component] = base->overrides;

			switch (component) {
			case toolchain_component_compiler: {
				args = toolchain_compiler_version(wk, comp);
				break;
			}
			case toolchain_component_linker: {
				args = toolchain_linker_version(wk, comp);
				break;
			}
			case toolchain_component_static_linker: {
				args = toolchain_static_linker_version(wk, comp);
				break;
			}
			}

			compiler->type[component] = 0;
			compiler->overrides[component] = 0;

			if (!args->len) {
				obj_array_push(wk, toolchains_with_no_version_arg, i);
				continue;
			}

			obj list = 0;
			if (!obj_dict_index_str(wk, toolchains_grouped_by_version_arg, args->args[0], &list)) {
				list = make_obj(wk, obj_array);
				obj_dict_set(wk, toolchains_grouped_by_version_arg, make_str(wk, args->args[0]), list);
			}

			obj_array_push(wk, list, i);
		}
	}

	struct toolchain_exe_detect_candidate default_candidate = { .score = INT64_MIN };

	// check each candidate
	for (uint32_t i = 0; i < candidates_len; ++i) {
		obj c = candidates[i];

		// normalize the candidate into a command array with a full path to
		// argv0, or skip if not found
		obj cmd_arr;
		if (do_linker_passthrough) {
			// This cmd_arr was already resolved since it is the compiler
			// cmd_arr
			cmd_arr = c;
		} else {
			obj argv0 = obj_array_index(wk, c, 0);

			obj found_prog = 0;
			struct find_program_ctx find_program_ctx = {
				.res = &found_prog,
				.requirement = requirement_required,
				.machine = compiler->machine,
			};

			if (!find_program(wk, &find_program_ctx, argv0) || !find_program_ctx.found) {
				continue;
			}

			struct obj_external_program *ep = get_obj_external_program(wk, found_prog);
			obj_array_dup(wk, ep->cmd_array, &cmd_arr);
			obj_array_extend(wk, cmd_arr, obj_array_slice(wk, c, 1, -1));
		}

		if (!default_candidate.found) {
			default_candidate.cmd_arr = cmd_arr;

			if (!toolchain_component_detect_check_list(wk,
				    component,
				    toolchains_with_no_version_arg,
				    make_str(wk, ""),
				    cmd_arr,
				    &default_candidate)) {
				return false;
			}
		}

		// check the candidate by querying it's output and running each
		// registered component's detect function
		{
			struct toolchain_exe_detect_candidate candidate = { .score = INT64_MIN };

			obj version_arg, list;
			obj_dict_for(wk, toolchains_grouped_by_version_arg, version_arg, list) {
				struct run_cmd_ctx cmd_ctx = { 0 };

				bool ok;
				if (do_linker_passthrough) {
					const char *argv[] = { get_cstr(wk, version_arg) };
					struct args args = { .args = argv, .len = 1 };
					ok = run_cmd_args(wk, &cmd_ctx, cmd_arr, toolchain_compiler_linker_passthrough(wk, comp, &args));
				} else {
					ok = run_cmd_arr(wk, &cmd_ctx, cmd_arr, get_cstr(wk, version_arg));
				}

				if (!ok) {
					run_cmd_ctx_destroy(&cmd_ctx);
					return false;
				}

				// if (cmd_ctx.status != 0) {
				// 	run_cmd_ctx_destroy(&cmd_ctx);
				// 	continue;
				// }

				TSTR(output);
				tstr_pushn(wk, &output, cmd_ctx.out.buf, cmd_ctx.out.len);
				tstr_pushn(wk, &output, cmd_ctx.err.buf, cmd_ctx.err.len);
				run_cmd_ctx_destroy(&cmd_ctx);

				if (!toolchain_component_detect_check_list(
					    wk, component, list, tstr_into_str(wk, &output), cmd_arr, &candidate)) {
					return false;
				}
			}

			if (!candidate.found) {
				goto check_next_candidate;
			}

			obj ver;
			if (!guess_version(wk, get_str(wk, candidate.output)->s, &ver)) {
				ver = make_str(wk, "unknown");
			}

			toolchain_component_detect_apply_candidate(compiler, component, &candidate, ver, found);
			return true;
		}

check_next_candidate:
		continue;
	}

	if (default_candidate.found) {
		// const struct toolchain_registry_component *base = arr_get(registry, default_candidate.idx);
		// L("unable to detect %s type, falling back on %s", toolchain_component_to_s(component), base->id.id);
		toolchain_component_detect_apply_candidate(
			compiler, component, &default_candidate, make_str(wk, "unknown"), found);
		return true;
	}

	LOG_W("unable to detect %s type", toolchain_component_to_s(component));
	return true;
}

static void
toolchain_component_compiler_populate_libdirs(struct workspace *wk, obj comp, struct obj_compiler *compiler)
{
	struct run_cmd_ctx cmd_ctx = { 0 };
	const struct args *args = toolchain_compiler_print_search_dirs(wk, comp);
	if (!args->len) {
		goto done;
	}

	if (!run_cmd_args(wk, &cmd_ctx, compiler->cmd_arr[toolchain_component_compiler], args)
		|| cmd_ctx.status) {
		goto done;
	}

	const char *key = "libraries: ";
	char *s, *e;
	bool beginning_of_line = true;
	for (s = cmd_ctx.out.buf; *s; ++s) {
		if (beginning_of_line && strncmp(s, key, strlen(key)) == 0) {
			s += strlen(key);
			if (*s == '=') {
				++s;
			}

			e = strchr(s, '\n');

			struct str str = {
				.s = s,
				.len = e ? (uint32_t)(e - s) : strlen(s),
			};

			compiler->libdirs = str_split(wk, &str, &STR(ENV_PATH_SEP_STR));
			goto done;
		}

		beginning_of_line = *s == '\n';
	}

done:
	run_cmd_ctx_destroy(&cmd_ctx);

	if (!compiler->libdirs) {
		L("populating libdirs using defaults");

		// TODO: make this a toolchain compiler handler
		const char *libdirs[] = { "/usr/lib", "/usr/local/lib", "/lib", NULL };

		compiler->libdirs = make_obj(wk, obj_array);

		uint32_t i;
		for (i = 0; libdirs[i]; ++i) {
			obj_array_push(wk, compiler->libdirs, make_str(wk, libdirs[i]));
		}
	}
}

static void
toolchain_component_compiler_refine_machine(struct workspace *wk, obj comp, struct obj_compiler *compiler)
{
	const struct args *args = toolchain_compiler_dumpmachine(wk, comp);
	if (!args->len) {
		return;
	}

	struct run_cmd_ctx cmd_ctx = { 0 };
	if (run_cmd_args(wk, &cmd_ctx, compiler->cmd_arr[toolchain_component_compiler], args)
		&& cmd_ctx.status == 0) {
		struct target_triple *t = &compiler->triple;
		machine_parse_triple(&TSTR_STR(&cmd_ctx.out), t);

		// make triple permanent by allocating it in the workspace
		struct str *parts[] = { &t->arch, &t->vendor, &t->system, &t->env };
		for (uint32_t i = 0; i < ARRAY_LEN(parts); ++i) {
			if (parts[i]->len) {
				const struct str *perm = get_str(wk, make_strn(wk, parts[i]->s, parts[i]->len));
				*parts[i] = *perm;
			}
		}
	}
	run_cmd_ctx_destroy(&cmd_ctx);
}

bool
toolchain_detect(struct workspace *wk,
	obj *comp,
	enum machine_kind machine,
	enum compiler_language lang,
	enum toolchain_detect_flag flags)
{
	if (obj_dict_geti(wk, wk->toolchains[machine], lang, comp)) {
		return true;
	}

	L("detecting %s", compiler_language_to_s(lang));

	*comp = make_obj(wk, obj_compiler);
	struct obj_compiler *compiler = get_obj_compiler(wk, *comp);
	compiler->machine = machine;
	compiler->lang = lang;

	for (uint32_t i = 0; i < toolchain_component_count; ++i) {
		bool found;
		if (!toolchain_component_detect(wk, i, *comp, &found) || !found) {
			return false;
		}

		if (i == toolchain_component_compiler) {
			toolchain_component_compiler_refine_machine(wk, *comp, compiler);
			toolchain_component_compiler_populate_libdirs(wk, *comp, compiler);
		}
	}

	obj_dict_seti(wk, wk->toolchains[machine], lang, *comp);

	if (flags & toolchain_detect_flag_silent) {
		return true;
	}

	LLOG_I("%s: detected", compiler_log_prefix(lang, machine));
	for (uint32_t i = 0; i < toolchain_component_count; ++i) {
		if (i > toolchain_component_compiler) {
			log_plain(log_info, ", %s:", toolchain_component_to_s(i));
		}

		log_plain(log_info, " %s", toolchain_component_type_to_id(wk, i, compiler->type[i])->id);

		const struct str *ver_str = get_str(wk, compiler->ver[i]);
		if (!str_eql(ver_str, &STR("unknown"))) {
			log_plain(log_info, " %s", ver_str->s);
		}

		if (i == toolchain_component_linker && toolchain_compiler_do_linker_passthrough(wk, *comp)) {
			log_plain(log_info, " (passed-through)");
		} else {
			obj_lprintf(wk, log_info, " (%o)", compiler->cmd_arr[i]);
		}
	}
	log_plain(log_info, "\n");
	return true;
}

/*******************************************************************************
 * Toolchain args
 ******************************************************************************/

#define TOOLCHAIN_ARGS(...)                      \
	static const char *argv[] = __VA_ARGS__; \
	static struct args args = { .args = argv, .len = ARRAY_LEN(argv) };

#define TOOLCHAIN_PROTO_0(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_0)
#define TOOLCHAIN_PROTO_1i(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1i)
#define TOOLCHAIN_PROTO_1s(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1s)
#define TOOLCHAIN_PROTO_2s(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_2s)
#define TOOLCHAIN_PROTO_1s1b(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1s1b)
#define TOOLCHAIN_PROTO_ns(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_ns)
#define TOOLCHAIN_PROTO_0rb(name) static bool name(TOOLCHAIN_SIG_0rb)
#define TOOLCHAIN_PROTO_1srb(name) static bool name(TOOLCHAIN_SIG_1srb)

/* empty functions */

TOOLCHAIN_PROTO_0(toolchain_arg_empty_0)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_1i(toolchain_arg_empty_1i)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_1s(toolchain_arg_empty_1s)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_2s(toolchain_arg_empty_2s)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_1s1b(toolchain_arg_empty_1s1b)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_ns(toolchain_arg_empty_ns)
{
	return n1;
}

TOOLCHAIN_PROTO_0rb(toolchain_arg_empty_0rb)
{
	return false;
}

TOOLCHAIN_PROTO_1srb(toolchain_arg_empty_1srb)
{
	return false;
}

const struct language languages[compiler_language_count] = {
	[compiler_language_null] = { 0 },
	[compiler_language_c] = { .is_header = false },
	[compiler_language_c_hdr] = { .is_header = true },
	[compiler_language_cpp] = { .is_header = false },
	[compiler_language_cpp_hdr] = { .is_header = true },
	[compiler_language_c_obj] = { .is_linkable = true },
	[compiler_language_assembly] = { 0 },
	[compiler_language_llvm_ir] = { 0 },
};

bool
toolchain_register_component(struct workspace *wk,
	enum toolchain_component component,
	const struct toolchain_registry_component *base)
{
	obj ids;
	if (!(ids = wk->toolchain_registry.ids[component])) {
		ids = wk->toolchain_registry.ids[component] = make_obj(wk, obj_dict);
	}

	obj _res;
	if (obj_dict_index_str(wk, ids, base->id.id, &_res)) {
		vm_error(wk, "toolchain %s already registered", base->id.id);
		return false;
	}

	struct arr *registry = &wk->toolchain_registry.components[component];
	uint32_t idx = registry->len;
	arr_grow_by(wk->a, registry, 1);
	struct toolchain_registry_component *dest = arr_get(registry, idx);
	*dest = *base;
	if (!dest->id.public_id) {
		dest->id.public_id = dest->id.id;
	}

	obj_dict_set(wk, ids, make_str(wk, base->id.id), make_number(wk, idx));
	return true;
}

void
compilers_init(struct workspace *wk)
{
	for (uint32_t i = 0; i < toolchain_component_count; ++i) {
		arr_init(wk->a, &wk->toolchain_registry.components[i], 16, struct toolchain_registry_component);
		toolchain_register_component(wk, i, &(struct toolchain_registry_component){ .id = { .id = "empty" } } );
	}

#define TOOLCHAIN_ENUM(v) vm_enum_value_prefixed(wk, compiler_optimization_lvl, v);
	vm_enum(wk, enum compiler_optimization_lvl);
	FOREACH_COMPILER_OPTIMIZATION_LVL(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
#define TOOLCHAIN_ENUM(v) vm_enum_value_prefixed(wk, compiler_pgo_stage, v);
	vm_enum(wk, enum compiler_pgo_stage);
	FOREACH_COMPILER_PGO_STAGE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
#define TOOLCHAIN_ENUM(v) vm_enum_value_prefixed(wk, compiler_warning_lvl, v);
	vm_enum(wk, enum compiler_warning_lvl);
	FOREACH_COMPILER_WARNING_LVL(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
#define TOOLCHAIN_ENUM(v) vm_enum_value_prefixed(wk, compiler_visibility_type, v);
	vm_enum(wk, enum compiler_visibility_type);
	FOREACH_COMPILER_VISIBILITY_TYPE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
}

enum toolchain_arg_arity {
	toolchain_arg_arity_0,
	toolchain_arg_arity_1i,
	toolchain_arg_arity_1s,
	toolchain_arg_arity_2s,
	toolchain_arg_arity_1s1b,
	toolchain_arg_arity_ns,
	toolchain_arg_arity_0rb,
	toolchain_arg_arity_1srb,
};

struct toolchain_arg_handler {
	const char *name;
	enum toolchain_arg_arity arity;
};

#define TOOLCHAIN_ARG_MEMBER_(name, comp, return_type, __type, params, names) { #name, toolchain_arg_arity_##__type },
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, comp, type)
static struct toolchain_arg_handler toolchain_compiler_arg_handlers[] = { FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER) };
static struct toolchain_arg_handler toolchain_linker_arg_handlers[] = { FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) };
static struct toolchain_arg_handler toolchain_static_linker_arg_handlers[]
	= { FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) };
#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

static struct {
	struct toolchain_arg_handler *handlers;
	uint32_t len;
} toolchain_arg_handlers[] = {
	[toolchain_component_compiler]
	= { toolchain_compiler_arg_handlers, ARRAY_LEN(toolchain_compiler_arg_handlers) },
	[toolchain_component_linker] = { toolchain_linker_arg_handlers, ARRAY_LEN(toolchain_linker_arg_handlers) },
	[toolchain_component_static_linker]
	= { toolchain_static_linker_arg_handlers, ARRAY_LEN(toolchain_static_linker_arg_handlers) },
};

bool
toolchain_overrides_validate(struct workspace *wk, uint32_t ip, obj handlers, enum toolchain_component component)
{
	const struct toolchain_arg_handler *handler;
	obj k, v;
	obj_dict_for(wk, handlers, k, v) {
		const struct str *name = get_str(wk, k);

		{
			uint32_t i;
			for (i = 0; i < toolchain_arg_handlers[component].len; ++i) {
				if (str_eql(&STRL(toolchain_arg_handlers[component].handlers[i].name), name)) {
					handler = &toolchain_arg_handlers[component].handlers[i];
					break;
				}
			}

			if (i == toolchain_arg_handlers[component].len) {
				vm_error_at(wk, ip, "unknown toolchain function %o", k);
				return false;
			}
		}

		const type_tag list_of_str = make_complex_type(wk, complex_type_nested, tc_array, tc_string);
		type_tag return_type = list_of_str;
		struct args_norm an[4] = { { tc_compiler }, { ARG_TYPE_NULL }, { ARG_TYPE_NULL }, { ARG_TYPE_NULL } };

		switch (handler->arity) {
		case toolchain_arg_arity_0: {
			break;
		}
		case toolchain_arg_arity_1i: {
			// arity 1i means it is an enum (i for integer) on the C side, and
			// a string on the meson side
			an[1].type = tc_string;
			break;
		}
		case toolchain_arg_arity_1s: {
			an[1].type = tc_string;
			break;
		}
		case toolchain_arg_arity_2s: {
			an[1].type = tc_string;
			an[2].type = tc_string;
			break;
		}
		case toolchain_arg_arity_1s1b: {
			an[1].type = tc_string;
			an[2].type = tc_bool;
			break;
		}
		case toolchain_arg_arity_ns: {
			an[1].type = list_of_str;
			break;
		}
		case toolchain_arg_arity_0rb: {
			return_type = tc_bool;
			break;
		}
		case toolchain_arg_arity_1srb: {
			an[1].type = tc_string;
			return_type = tc_bool;
			break;
		}
		default: UNREACHABLE;
		}

		if (get_obj_type(wk, v) == obj_capture) {
			if (!typecheck_capture(wk, ip, v, an, 0, return_type, get_str(wk, k)->s)) {
				return false;
			}

			if (handler->arity == toolchain_arg_arity_1i) {
				// patch the function signature with the str argument swapped
				// for the proper str_enum and re-analyze it
				type_tag enum_type = 0;
				if (str_eql(name, &STR("optimization"))) {
					enum_type = complex_type_enum_get(wk, enum compiler_optimization_lvl);
				} else if (str_eql(name, &STR("pgo"))) {
					enum_type = complex_type_enum_get(wk, enum compiler_pgo_stage);
				} else if (str_eql(name, &STR("visibility"))) {
					enum_type = complex_type_enum_get(wk, enum compiler_visibility_type);
				} else if (str_eql(name, &STR("warning_lvl"))) {
					enum_type = complex_type_enum_get(wk, enum compiler_warning_lvl);
				}

				if (enum_type) {
					struct obj_capture *capture = get_obj_capture(wk, v);
					capture->func->an[1].type = enum_type;

					if (wk->vm.in_analyzer) {
						az_analyze_func(wk, v);
					}
				}
			}
		} else if (!typecheck_custom(wk, 0, v, return_type, 0)) {
			vm_error_at(wk,
				ip,
				"expected value type %s for handler %o with constant return value, got %s\n",
				typechecking_type_to_s(wk, return_type),
				k,
				obj_typestr(wk, v));
			return false;
		}
	}

	return true;
}

static obj
lookup_toolchain_arg_override(struct workspace *wk,
	obj compiler,
	enum toolchain_component component,
	uint32_t arg)
{
	struct obj_compiler *c = get_obj_compiler(wk, compiler);
	obj overrides = c->overrides[component], override;
	if (overrides
		&& obj_dict_index_str(wk, overrides, toolchain_arg_handlers[component].handlers[arg].name, &override)) {
		return override;
	}

	return 0;
}

enum toolchain_arg_by_component {
#define TOOLCHAIN_ARG_MEMBER_(comp, _name) toolchain_arg_by_component_##comp##_name,
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(comp, _##name)
	FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER) toolchain_arg_by_component_reset_0 = -1,
	FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) toolchain_arg_by_component_reset_1 = -1,
	FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_
};

static obj handle_toolchain_arg_override;

static const struct args *
handle_toolchain_arg_override_returning_args(struct workspace *wk, struct args_norm *an)
{
	obj list = 0;
	if (get_obj_type(wk, handle_toolchain_arg_override) == obj_array) {
		list = handle_toolchain_arg_override;
	} else {
		if (!vm_eval_capture(wk, handle_toolchain_arg_override, an, 0, &list)) {
			UNREACHABLE;
		}
	}

	static const char *argv[32];
	static struct args args;
	memset(argv, 0, sizeof(argv));
	args = (struct args){ .args = argv, .len = 0 };

	obj v;
	obj_array_for(wk, list, v) {
		assert(args.len < ARRAY_LEN(argv) && "increase size of argv");

		argv[args.len] = get_cstr(wk, v);
		++args.len;
	}

	return &args;
}

static bool
handle_toolchain_arg_override_returning_bool(struct workspace *wk, struct args_norm *an)
{
	if (get_obj_type(wk, handle_toolchain_arg_override) == obj_bool) {
		return get_obj_bool(wk, handle_toolchain_arg_override);
	}

	obj b;
	if (!vm_eval_capture(wk, handle_toolchain_arg_override, an, 0, &b)) {
		UNREACHABLE;
	}

	return get_obj_bool(wk, b);
}

static const struct args *
handle_toolchain_arg_override_0(TOOLCHAIN_SIG_0)
{
	struct args_norm an[] = { { .val = comp }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an);
}

static const struct args *
handle_toolchain_arg_override_1i(TOOLCHAIN_SIG_1i)
{
	struct args_norm an[] = { { .val = comp }, { .val = i1 }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an);
}

static const struct args *
handle_toolchain_arg_override_1s(TOOLCHAIN_SIG_1s)
{
	struct args_norm an[] = { { .val = comp }, { .val = make_str(wk, s1) }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an);
}

static const struct args *
handle_toolchain_arg_override_2s(TOOLCHAIN_SIG_2s)
{
	struct args_norm an[] = { { .val = comp }, { .val = make_str(wk, s1) }, { .val = make_str(wk, s2) }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an);
}

static const struct args *
handle_toolchain_arg_override_1s1b(TOOLCHAIN_SIG_1s1b)
{
	struct args_norm an[]
		= { { .val = comp }, { .val = make_str(wk, s1) }, { .val = b1 ? obj_bool_true : obj_bool_false }, { ARG_TYPE_NULL }, };
	return handle_toolchain_arg_override_returning_args(wk, an);
}

static const struct args *
handle_toolchain_arg_override_ns(TOOLCHAIN_SIG_ns)
{
	obj list = make_obj(wk, obj_array);
	push_args(wk, list, n1);

	struct args_norm an[] = { { .val = comp }, { .val = list }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an);
}

static bool
handle_toolchain_arg_override_0rb(TOOLCHAIN_SIG_0rb)
{
	struct args_norm an[] = { { .val = comp }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_bool(wk, an);
}

static bool
handle_toolchain_arg_override_1srb(TOOLCHAIN_SIG_1srb)
{
	struct args_norm an[] = { { .val = comp }, { .val = make_str(wk, s1) }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_bool(wk, an);
}

#define TOOLCHAIN_ARG_MEMBER_(name, _name, component, return_type, _type, params, names)                           \
	return_type toolchain_##component##_name params                                                            \
	{                                                                                                          \
		handle_toolchain_arg_override = lookup_toolchain_arg_override(                                     \
			wk, comp, toolchain_component_##component, toolchain_arg_by_component_##component##_name); \
		if (handle_toolchain_arg_override) {                                                               \
			return handle_toolchain_arg_override_##_type names;                                        \
		}                                                                                                  \
		return toolchain_arg_empty_ ##_type names;                                                                    \
	}

#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, _##name, comp, type)

FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER)
FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)

#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

static void
toolchain_print_dumped_args(const struct args *args)
{
	if (args) {
		printf("{");
		for (uint32_t i = 0; i < args->len; ++i) {
			printf("\"%s\"", args->args[i]);

			if (i + 1 < args->len) {
				printf(", ");
			}
		}
		printf("}");
	} else {
		printf("false");
	}

	printf("\n");
}

static void
toolchain_print_dumped_bool(bool v)
{
	printf("%s\n", v ? "true" : "false");
}

static void
toolchain_dump_args_0(const struct args *args)
{
	toolchain_print_dumped_args(args);
}

static void
toolchain_dump_args_1i(const struct args *args)
{
	toolchain_print_dumped_args(args);
}

static void
toolchain_dump_args_1s(const struct args *args)
{
	toolchain_print_dumped_args(args);
}

static void
toolchain_dump_args_2s(const struct args *args)
{
	toolchain_print_dumped_args(args);
}

static void
toolchain_dump_args_1s1b(const struct args *args)
{
	toolchain_print_dumped_args(args);
}

static void
toolchain_dump_args_ns(const struct args *args)
{
	toolchain_print_dumped_args(args);
}

static void
toolchain_dump_args_0rb(bool v)
{
	toolchain_print_dumped_bool(v);
}

static void
toolchain_dump_args_1srb(bool v)
{
	toolchain_print_dumped_bool(v);
}

void
toolchain_dump(struct workspace *wk, obj comp, struct toolchain_dump_opts *opts)
{
	const char *s1 = opts->s1, *s2 = opts->s2;
	const bool b1 = opts->b1;
	const uint32_t i1 = opts->i1;
	const struct args *n1 = opts->n1;

	printf("%-13s %-25s %-4s %s\n", "component", "name", "sig", "args");
	printf("%-13s %-25s %-4s %s\n", "---", "---", "---", "---");

#define TOOLCHAIN_ARG_MEMBER_(name, _name, component, return_type, _type, params, names) \
	printf("%-13s %-25s %-4s ", #component, #name, #_type);                          \
	toolchain_dump_args_##_type(toolchain_##component##_name names);
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, _##name, comp, type)

	FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER)
	FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
	FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)

#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_
}

const char *
compiler_log_prefix(enum compiler_language lang, enum machine_kind machine)
{
	static char buf[256];

	if (machine == machine_kind_build) {
		snprintf(buf,
			sizeof(buf),
			"%s %s machine compiler",
			compiler_language_to_s(lang),
			machine_kind_to_s(machine));
	} else {
		snprintf(buf, sizeof(buf), "%s compiler", compiler_language_to_s(lang));
	}

	return buf;
}
