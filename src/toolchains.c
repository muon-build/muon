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
#include "formats/ansi.h"
#include "functions/external_program.h"
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
	[toolchain_component_archiver] = { "archiver" },
};

const char *
toolchain_component_to_s(enum toolchain_component comp)
{
	return toolchain_component_name[comp].id;
}

bool
toolchain_component_from_s(struct workspace *wk, const char *name, uint32_t *res)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(toolchain_component_name); ++i) {
		if (strcmp(name, toolchain_component_name[i].id) == 0) {
			*res = i;
			return true;
		}
	}

	if (strcmp(name, "static_linker") == 0) {
		vm_deprecation_at(wk, 0, "0.6.0", "static_linker has been renamed to archiver");
		*res = toolchain_component_archiver;
		return true;
	}

	return false;
}

#if 0
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
	[compiler_language_masm] = "masm",
	[compiler_language_rust] = "rust",
	[compiler_language_vala] = "vala",
	[compiler_language_fortran] = "fortran",
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
#endif

const struct language *
toolchain_language_get(struct workspace *wk, struct compiler_language l)
{
	return arr_get(&wk->language_registry.languages, l.id);
}

const char *
compiler_language_to_s(struct workspace *wk, struct compiler_language l)
{
	return toolchain_language_get(wk, l)->name.s;
}

bool
s_to_compiler_language(struct workspace *wk, const struct str *s, struct compiler_language *l)
{
	uint32_t i;
	for (i = 0; i < wk->language_registry.languages.len; ++i) {
		const struct language *lang = &((struct language *)wk->language_registry.languages.e)[i];
		if (str_eqli(s, &lang->name)) {
			*l = (struct compiler_language){ i };
			return true;
		}
	}

	return false;
}

bool
extension_to_compiler_language(struct workspace *wk, const struct str *ext, struct compiler_language *l)
{
	if (!ext->len) {
		return false;
	}

	for (uint32_t i = 0; i < wk->language_registry.languages.len; ++i) {
		const struct language *lang = &((struct language *)wk->language_registry.languages.e)[i];
		if (!lang->exts) {
			continue;
		}

		obj e;
		obj_array_for(wk, lang->exts, e) {
			if (str_eql(get_str(wk, e), ext)) {
				*l = (struct compiler_language){ i };
				return true;
			}
		}
	}

	return false;
}

bool
filename_to_compiler_language(struct workspace *wk, const struct str *s, struct compiler_language *l)
{
	struct str_cut cut;

	if (!str_rcut(s, '.', &cut)) {
		return false;
	}

	const struct str ext = cut.after;

	return extension_to_compiler_language(wk, &ext, l);
}

const char *
compiler_language_extension(struct workspace *wk, struct compiler_language l)
{
	return get_str(wk, obj_array_index(wk, toolchain_language_get(wk, l)->exts, 0))->s;
}

static bool
coalesce_link_languages_(struct workspace *wk, struct compiler_language cur, struct compiler_language new, struct compiler_language *res)
{
	const struct language *new_l = toolchain_language_get(wk, new);
	if (!new_l->link_language.id) {
		*res = cur;
		return true;
	}

	new = new_l->link_language;
	new_l = toolchain_language_get(wk, new);

	if (!cur.id) {
		*res = new;
		return true;
	}

	const struct language *cur_l = toolchain_language_get(wk, cur);
	if (cur_l->coalesce_link_language) {
		struct args_norm an[] = { { .val = make_str(wk, new_l->name.s) }, { ARG_TYPE_NULL } };
		obj fn_res;
		if (!vm_eval_closure(wk, cur_l->coalesce_link_language, an, 0, &fn_res)) {
			return false;
		}
		if (!s_to_compiler_language(wk, get_str(wk, fn_res), res)) {
			vm_error(wk, "%o is not a valid language", fn_res);
			return false;
		}
	} else {
		*res = cur;
	}
	return true;
}

bool
coalesce_link_languages(struct workspace *wk, struct compiler_language cur, struct compiler_language new, struct compiler_language *res)
{
	bool ok = coalesce_link_languages_(wk, cur, new, res);
	L("coalesce %s -> %s == %s (%d)",
		compiler_language_to_s(wk, cur),
		compiler_language_to_s(wk, new),
		compiler_language_to_s(wk, *res),
		ok);
	return ok;
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
run_cmd_args(struct workspace *wk, struct run_cmd_ctx *cmd_ctx, obj cmd_arr, obj args)
{
	obj cmd_arr_to_run;
	obj_array_dup(wk, cmd_arr, &cmd_arr_to_run);
	obj_array_extend(wk, cmd_arr_to_run, args);
	return run_cmd_arr(wk, cmd_ctx, cmd_arr_to_run, 0);
}

struct toolchain_exe_detect_candidate {
	int64_t score;
	uint32_t idx;
	obj output;
	obj cmd_arr;
	obj overrides;
	obj ver;
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
			if (!vm_eval_closure(wk, rc->detect, detect_an, 0, &res)) {
				return false;
			}

			score = get_obj_number(wk, res);
		}

		if (score > 0 && score > candidate->score) {
			L("  '%s' new high score: %" PRId64, rc->id.id, score);
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
	bool *found)
{
	compiler->cmd_arr[component] = candidate->cmd_arr;
	compiler->type[component] = candidate->idx;
	compiler->ver[component] = candidate->ver;
	compiler->overrides[component] = candidate->overrides;
	if (component == toolchain_component_compiler) {
		compiler->ver_raw = candidate->output;
	}
	*found = true;
}

bool
obj_dict_get_language(struct workspace *wk, obj dict, const struct compiler_language l, obj *res)
{
	return obj_dict_geti(wk, dict, l.id, res);
}

void
obj_dict_set_language(struct workspace *wk, obj dict, const struct compiler_language l, obj v)
{
	obj_dict_seti(wk, dict, l.id, v);
}

static bool
toolchain_component_detect(struct workspace *wk,
	enum toolchain_component component,
	obj comp,
	bool *found,
	bool *set_from_option)
{
	L("> detecting component %s", toolchain_component_to_s(component));

	struct obj_compiler *compiler = get_obj_compiler(wk, comp);
	const struct arr *registry = &wk->toolchain_registry.components[component];

	*found = false;
	*set_from_option = false;

	// determine candidate executable list

	obj *candidates = 0;
	uint32_t candidates_len = 0;
	uint32_t preferred_type = 0; // TODO: use this do assist in detection of linker / archiver

	// first check if the option for this component has been set, e.g. env.CC
	{
		TSTR(opt_name);
		if (toolchain_component_option_name(wk, compiler->lang, component, compiler->machine, &opt_name)) {
			obj cmd_arr_opt = 0;
			if (!get_option(wk, current_project(wk), &TSTR_STR(&opt_name), &cmd_arr_opt)) {
				UNREACHABLE;
			}
			struct obj_option *cmd_arr = get_obj_option(wk, cmd_arr_opt);

			if (cmd_arr->source > option_value_source_default && get_obj_array(wk, cmd_arr->val)->len) {
				candidates = ar_maken(wk->a_scratch, obj, 1);
				candidates_len = 1;
				candidates[0] = cmd_arr->val;
				*set_from_option = true;
				LO("  using candidate exe from option %s: %o\n", opt_name.buf, candidates[0]);
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
				if (!obj_dict_get_language(wk, base->exe, compiler->lang, &exe)) {
					continue;
				}

				obj_dict_set(wk, exe_list, exe, obj_bool_true);
			}
			candidates_len = get_obj_dict(wk, exe_list)->len;

			if (!candidates_len) {
				LOG_E("no candidate executables defined for language %s", compiler_language_to_s(wk, compiler->lang));
				return false;
			}

			LO("  using candidate exe list: %o\n", exe_list);

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
				if (!vm_eval_closure(wk, rc->sub_components[component].fn, an, 0, &res)) {
					return false;
				}

				if (!toolchain_component_type_from_s(wk, component, get_cstr(wk, res), &preferred_type)) {
					vm_error_at(wk,
						get_obj_closure(wk, rc->sub_components[component].fn)->func->entry,
						"unknown %s type returned from toolchain function",
						toolchain_component_to_s(component));
					return false;
				}
			} else if (rc->sub_components[component].type) {
				preferred_type = rc->sub_components[component].type;
			} else {
				L("skipping %s detecting for %s compiler",
					toolchain_component_to_s(component),
					compiler_language_to_s(wk, compiler->lang));
				*found = true;
				return true;
			}

			if ((component == toolchain_component_linker && toolchain_compiler_do_linker_passthrough(wk, comp))
				|| (component == toolchain_component_archiver && toolchain_compiler_do_archiver_passthrough(wk, comp))) {
				do_linker_passthrough = true;
				candidates[0] = compiler->cmd_arr[toolchain_component_compiler];
			} else {
				const struct toolchain_registry_component *sub_component = arr_get(registry, preferred_type);
				candidates[0] = make_obj(wk, obj_array);
				obj_array_push(wk, candidates[0], sub_component->exe);
			}

			LO("  using candidate: %o\n", candidates[0]);
		}
	}

	// now determine lists of toolchains to check against, grouped by common
	// version argument.  The default_list is composed of toolchains with no
	// version argument defined.
	obj toolchains_grouped_by_version_arg = make_obj(wk, obj_dict);
	obj toolchains_with_no_version_arg = make_obj(wk, obj_array);
	{
		for (uint32_t i = 1 /* skip the empty toolchain */; i < registry->len; ++i) {
			obj args = 0;
			const struct toolchain_registry_component *base = arr_get(registry, i);

			if (preferred_type && i != preferred_type) {
				continue;
			} else if (component == toolchain_component_compiler) {
				obj _v;
				if (!obj_dict_get_language(wk, base->exe, compiler->lang, &_v)) {
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
			case toolchain_component_archiver: {
				args = toolchain_archiver_version(wk, comp);
				break;
			}
			}

			compiler->type[component] = 0;
			compiler->overrides[component] = 0;

			if (!get_obj_array(wk, args)->len) {
				obj_array_push(wk, toolchains_with_no_version_arg, i);
				continue;
			}

			obj head = obj_array_get_head(wk, args);

			obj list = 0;
			if (!obj_dict_index(wk, toolchains_grouped_by_version_arg, head, &list)) {
				list = make_obj(wk, obj_array);
				obj_dict_set(wk, toolchains_grouped_by_version_arg, head, list);
			}

			obj_array_push(wk, list, i);
		}
	}

	struct toolchain_exe_detect_candidate default_candidate
		= { .score = INT64_MIN, .ver = make_str(wk, "unknown") };
	struct toolchain_exe_detect_candidate candidate = { .score = INT64_MIN };

	// check each candidate
	for (uint32_t i = 0; i < candidates_len; ++i) {
		obj c = candidates[i];

		// normalize the candidate into a command array with a full path to
		// argv0, or skip if not found
		obj cmd_arr = 0;
		if (do_linker_passthrough) {
			// This cmd_arr was already resolved since it is the compiler
			// cmd_arr
			cmd_arr = c;
		} else {
			obj argv0 = obj_array_get_head(wk, c);

			obj found_prog = 0;
			struct find_program_ctx find_program_ctx = {
				.res = &found_prog,
				.requirement = requirement_required,
				.machine = compiler->machine,
			};

			if (!find_program(wk, &find_program_ctx, argv0) || !find_program_ctx.found) {
				LO("  skipping candidate %o: unable to find argv0 program\n", c);
				continue;
			}

			struct obj_external_program *ep = get_obj_external_program(wk, found_prog);
			obj ep_cmd_arr = obj_external_program_cmd_array(wk, ep, 0);
			obj_array_dup(wk, ep_cmd_arr, &cmd_arr);
			obj_array_extend(wk, cmd_arr, obj_array_slice(wk, c, 1, get_obj_array(wk, c)->len));
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
			obj version_arg, list;
			obj_dict_for(wk, toolchains_grouped_by_version_arg, version_arg, list) {
				struct run_cmd_ctx cmd_ctx = { 0 };

				bool ok;
				if (do_linker_passthrough) {
					obj argv = make_obj(wk, obj_array);
					obj_array_push(wk, argv, version_arg);
					ok = run_cmd_args(wk, &cmd_ctx, cmd_arr, toolchain_compiler_linker_passthrough(wk, comp, argv));
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
				LO("  skipping candidate %o: no detect function returned a score > 0\n", c);
				goto check_next_candidate;
			}

			if (!guess_version(wk, get_str(wk, candidate.output)->s, &candidate.ver)) {
				candidate.ver = make_str(wk, "unknown");
			}
		}

check_next_candidate:
		continue;
	}

	if (candidate.found) {
		toolchain_component_detect_apply_candidate(compiler, component, &candidate, found);
		return true;
	} else if (default_candidate.found) {
		// const struct toolchain_registry_component *base = arr_get(registry, default_candidate.idx);
		// L("unable to detect %s type, falling back on %s", toolchain_component_to_s(component), base->id.id);
		toolchain_component_detect_apply_candidate(
			compiler, component, &default_candidate, found);
		return true;
	}

	LOG_W("unable to detect %s type", toolchain_component_to_s(component));
	return true;
}

static void
toolchain_component_compiler_populate_libdirs(struct workspace *wk, obj comp, struct obj_compiler *compiler)
{
	struct run_cmd_ctx cmd_ctx = { 0 };
	obj args = toolchain_compiler_print_search_dirs(wk, comp);
	if (!get_obj_array(wk, args)->len) {
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
		L("  populating libdirs using defaults");

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
	obj args = toolchain_compiler_dumpmachine(wk, comp);
	if (!get_obj_array(wk, args)->len) {
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

static bool
toolchain_component_compiler_apply_wrapper(struct workspace *wk, struct obj_compiler *compiler, bool set_from_option)
{

	obj compiler_wrapper_opt;
	get_option_value(wk, NULL, "b_compiler_wrapper", &compiler_wrapper_opt);
	const struct str *compiler_wrapper = get_str(wk, compiler_wrapper_opt);

	bool is_auto = false;
	if (str_eql(compiler_wrapper, &STR("none"))) {
		return true;
	} else if (str_eql(compiler_wrapper, &STR("auto"))) {
		if (set_from_option
			/*|| !(compiler->lang == compiler_language_c
				|| compiler->lang == compiler_language_cpp
				|| compiler->lang == compiler_language_objc
				|| compiler->lang == compiler_language_objcpp) */) {
			return true;
		}
		is_auto = true;
	}

	workspace_scratch_begin(wk);

	struct arr wrappers = { 0 };
	arr_init(wk->a_scratch, &wrappers, 4, obj);

	if (is_auto) {
		obj o = make_str(wk, "sccache");
		arr_push(wk->a_scratch, &wrappers, &o);
		o = make_str(wk, "ccache");
		arr_push(wk->a_scratch, &wrappers, &o);
	} else {
		arr_push(wk->a_scratch, &wrappers, &compiler_wrapper_opt);
	}


	bool found = false;
	for (uint32_t i = 0; i < wrappers.len; ++i) {
		obj wrapper = *(obj*)arr_get(&wrappers, i), found_wrapper = 0;
		struct find_program_ctx ctx = {
			.res = &found_wrapper,
			.machine = compiler->machine,
		};

		if (find_program(wk, &ctx, wrapper) && ctx.found) {
			LOG_N("using compiler wrapper %s", get_cstr(wk, wrapper));
			struct obj_external_program *wrap_ep = get_obj_external_program(wk, found_wrapper);
			obj cmd_arr = compiler->cmd_arr[toolchain_component_compiler];
			obj new_cmd_arr = make_obj(wk, obj_array);

			obj_array_extend(wk, new_cmd_arr, obj_external_program_cmd_array(wk, wrap_ep, 0));
			obj_array_extend(wk, new_cmd_arr, cmd_arr);
			compiler->cmd_arr[toolchain_component_compiler] = new_cmd_arr;
			found = true;
			break;
		}
	}

	bool ok = true;
	if (!found && !is_auto) {
		LOG_E("failed to use compiler wrapper %s", get_cstr(wk, compiler_wrapper_opt));
		ok = false;
	}

	workspace_scratch_end(wk);

	return ok;
}

bool
toolchain_detect(struct workspace *wk,
	obj *comp,
	enum machine_kind machine,
	struct compiler_language lang,
	enum toolchain_detect_flag flags)
{
	if (obj_dict_get_language(wk, wk->toolchains[machine], lang, comp)) {
		return true;
	}

	L("detecting toolchain for language %s and %s machine",
		compiler_language_to_s(wk, lang),
		machine_kind_to_s(machine));

	*comp = make_obj(wk, obj_compiler);
	struct obj_compiler *compiler = get_obj_compiler(wk, *comp);
	compiler->machine = machine;
	compiler->lang = lang;

	for (uint32_t i = 0; i < toolchain_component_count; ++i) {
		bool found, set_from_option;
		if (!toolchain_component_detect(wk, i, *comp, &found, &set_from_option) || !found) {
			return false;
		}

		if (i == toolchain_component_compiler) {
			toolchain_component_compiler_refine_machine(wk, *comp, compiler);
			toolchain_component_compiler_populate_libdirs(wk, *comp, compiler);
			if (!toolchain_component_compiler_apply_wrapper(wk, compiler, set_from_option)) {
				return false;
			}
		}
	}

	obj_dict_set_language(wk, wk->toolchains[machine], lang, *comp);

	for (uint32_t i = 0; i < toolchain_component_count; ++i) {
		if (!compiler->ver[i]) {
			compiler->ver[i] = make_str(wk, "unknown");
		}
	}

	if (!(flags & toolchain_detect_flag_silent)) {
		LLOG_I("%s: ", compiler_log_prefix(wk, lang, machine));
		for (uint32_t i = 0; i < toolchain_component_count; ++i) {
			if (i) {
				log_plain(log_info, "\n  ");
				log_plain(log_info, "%s: ", toolchain_component_to_s(i));
			}

			log_plain(log_info,
				CLR(c_bold) CLR(c_green) "%s" CLR(0),
				toolchain_component_type_to_id(wk, i, compiler->type[i])->id);

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
	}

	{
		const struct arr *registry = &wk->toolchain_registry.components[toolchain_component_compiler];
		const struct toolchain_registry_component *base = arr_get(registry, compiler->type[toolchain_component_compiler]);
		if (base->on_detect) {
			struct args_norm an[] = { { .val = *comp }, { ARG_TYPE_NULL } };
			obj _res;
			if (!vm_eval_closure(wk, base->on_detect, an, 0, &_res)) {
				return false;
			}
		}
	}

	return true;
}

/*******************************************************************************
 * Toolchain args
 ******************************************************************************/

#define TOOLCHAIN_PROTO_0(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_0)
#define TOOLCHAIN_PROTO_0rs(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_0rs)
#define TOOLCHAIN_PROTO_1i(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1i)
#define TOOLCHAIN_PROTO_1n(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1n)
#define TOOLCHAIN_PROTO_1s(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1s)
#define TOOLCHAIN_PROTO_1srs(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1srs)
#define TOOLCHAIN_PROTO_2s(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_2s)
#define TOOLCHAIN_PROTO_2srs(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_2srs)
#define TOOLCHAIN_PROTO_1s1b(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1s1b)
#define TOOLCHAIN_PROTO_ns(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_ns)
#define TOOLCHAIN_PROTO_kw(name) static TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_kw)
#define TOOLCHAIN_PROTO_0rb(name) static bool name(TOOLCHAIN_SIG_0rb)
#define TOOLCHAIN_PROTO_1srb(name) static bool name(TOOLCHAIN_SIG_1srb)

/* empty functions */

TOOLCHAIN_PROTO_0(toolchain_arg_empty_0)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_0rs(toolchain_arg_empty_0rs)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_1i(toolchain_arg_empty_1i)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_1n(toolchain_arg_empty_1n)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_1s(toolchain_arg_empty_1s)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_1srs(toolchain_arg_empty_1srs)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_2s(toolchain_arg_empty_2s)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_2srs(toolchain_arg_empty_2srs)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_1s1b(toolchain_arg_empty_1s1b)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_ns(toolchain_arg_empty_ns)
{
	return ns1;
}

TOOLCHAIN_PROTO_kw(toolchain_arg_empty_kw)
{
	return make_obj(wk, obj_array);
}

TOOLCHAIN_PROTO_0rb(toolchain_arg_empty_0rb)
{
	return false;
}

TOOLCHAIN_PROTO_1srb(toolchain_arg_empty_1srb)
{
	return false;
}

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

enum toolchain_arg_arity {
	toolchain_arg_arity_0,
	toolchain_arg_arity_0rs,
	toolchain_arg_arity_1i,
	toolchain_arg_arity_1n,
	toolchain_arg_arity_1s,
	toolchain_arg_arity_1srs,
	toolchain_arg_arity_2s,
	toolchain_arg_arity_2srs,
	toolchain_arg_arity_1s1b,
	toolchain_arg_arity_ns,
	toolchain_arg_arity_kw,
	toolchain_arg_arity_0rb,
	toolchain_arg_arity_1srb,
};

struct toolchain_handler_info {
	const char *name;
	enum toolchain_arg_arity arity;
	const char *desc;
	const char *enum_arg;
	struct args_norm an[4];
	struct args_kw akw[2];
	struct typecheck_closure_sig sig;
};

#define TOOLCHAIN_ARG_MEMBER_(name, comp, return_type, __type, params, names) { #name, toolchain_arg_arity_##__type },
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, comp, type)
static struct toolchain_handler_info toolchain_compiler_handler_info[] = { FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER) };
static struct toolchain_handler_info toolchain_linker_handler_info[] = { FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) };
static struct toolchain_handler_info toolchain_archiver_handler_info[]
	= { FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) };
#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

#define TOOLCHAIN_ARG_MEMBER_(name, return_type, __type, params, names) { #name, toolchain_arg_arity_##__type },
#define TOOLCHAIN_ARG_MEMBER(name, type) TOOLCHAIN_ARG_MEMBER_(name, type)
static struct toolchain_handler_info language_snippet_handler_info[] = { FOREACH_LANGUAGE_SNIPPET(TOOLCHAIN_ARG_MEMBER) };
#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

static struct {
	struct toolchain_handler_info *handlers;
	uint32_t len;
} toolchain_arg_handlers[] = {
	[toolchain_component_compiler]
	= { toolchain_compiler_handler_info, ARRAY_LEN(toolchain_compiler_handler_info) },
	[toolchain_component_linker] = { toolchain_linker_handler_info, ARRAY_LEN(toolchain_linker_handler_info) },
	[toolchain_component_archiver]
	= { toolchain_archiver_handler_info, ARRAY_LEN(toolchain_archiver_handler_info) },
};

static struct toolchain_handler_info *
toolchain_handler_info_get(enum toolchain_component component, const char *n)
{
	const struct str *name = &STRL(n);
	for (uint32_t i = 0; i < toolchain_arg_handlers[component].len; ++i) {
		if (str_eql(&STRL(toolchain_arg_handlers[component].handlers[i].name), name)) {
			return &toolchain_arg_handlers[component].handlers[i];
		}
	}
	return 0;
}

static struct toolchain_handler_info *
language_snippet_info_get(const char *n)
{
	const struct str *name = &STRL(n);
	for (uint32_t i = 0; i < ARRAY_LEN(language_snippet_handler_info); ++i) {
		if (str_eql(&STRL(language_snippet_handler_info[i].name), name)) {
			return &language_snippet_handler_info[i];
		}
	}
	return 0;
}

static void
toolchain_handler_info_doc(enum toolchain_component component, const char *name, const struct toolchain_handler_info *src)
{
	struct toolchain_handler_info *info;
	if (!(info = toolchain_handler_info_get(component, name))) {
		UNREACHABLE;
	}

	info->desc = src->desc;
	info->enum_arg = src->enum_arg;
}

static void
language_snippet_info_doc(const char *name, const struct toolchain_handler_info *src)
{
	struct toolchain_handler_info *info;
	if (!(info = language_snippet_info_get(name))) {
		UNREACHABLE;
	}

	info->desc = src->desc;
	info->enum_arg = src->enum_arg;
}

static void
handler_allow_any_kw(struct workspace *wk, struct toolchain_handler_info *handler)
{
	struct args_kw *akw = handler->akw;
	akw[0].key = "kwargs";
	akw[0].type = TYPE_TAG_GLOB | tc_any;
	handler->sig.akw = akw;
}

static void
handler_array_info_init(struct workspace *wk, struct toolchain_handler_info *arr, const uint32_t len)
{
	const type_tag list_of_str = complex_type_preset_get(wk, tc_cx_list_of_str);
	const type_tag str_or_list_of_str = complex_type_preset_get(wk, tc_cx_str_or_list_of_str);
	for (uint32_t i = 0; i < len; ++i) {
		struct toolchain_handler_info *handler = &arr[i];
		handler->sig.return_type = list_of_str;
		assert(handler->desc);
		struct args_norm *an = handler->an;
		an[0].type = tc_compiler;
		an[1].type = ARG_TYPE_NULL;
		an[2].type = ARG_TYPE_NULL;
		an[3].type = ARG_TYPE_NULL;

		switch (handler->arity) {
		case toolchain_arg_arity_0: {
			break;
		}
		case toolchain_arg_arity_0rs: {
			handler_allow_any_kw(wk, handler);
			an[0].type = ARG_TYPE_NULL;
			handler->sig.return_type = str_or_list_of_str;
			break;
		}
		case toolchain_arg_arity_1i: {
			// arity 1i means it is an enum converted to an string obj on the C side, and
			// a string on the meson side
			assert(handler->enum_arg);
			an[1].type = complex_type_enum_get_(wk, handler->enum_arg);
			break;
		}
		case toolchain_arg_arity_1n: {
			an[1].type = tc_number;
			break;
		}
		case toolchain_arg_arity_1s: {
			an[1].type = tc_string;
			break;
		}
		case toolchain_arg_arity_1srs: {
			handler_allow_any_kw(wk, handler);
			an[0].type = tc_string;
			handler->sig.return_type = str_or_list_of_str;
			break;
		}
		case toolchain_arg_arity_2s: {
			an[1].type = tc_string;
			an[2].type = tc_string;
			break;
		}
		case toolchain_arg_arity_2srs: {
			handler_allow_any_kw(wk, handler);
			an[0].type = tc_string;
			an[1].type = tc_string;
			handler->sig.return_type = str_or_list_of_str;
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
		case toolchain_arg_arity_kw: {
			handler_allow_any_kw(wk, handler);
			break;
		}
		case toolchain_arg_arity_0rb: {
			handler->sig.return_type = tc_bool;
			break;
		}
		case toolchain_arg_arity_1srb: {
			an[1].type = tc_string;
			handler->sig.return_type = tc_bool;
			break;
		}
		default: UNREACHABLE;
		}

		handler->sig.an = an;
	}
}

static void
toolchain_handler_info_init(struct workspace *wk)
{
#define doc(name_, comp_, ...) \
	toolchain_handler_info_doc(toolchain_component_##comp_, #name_, &(struct toolchain_handler_info){ __VA_ARGS__ })

	// compiler
	doc(always, compiler, .desc = "Arguments that are almost always passed, except in special cases such as detection.");
	doc(argument_syntax, compiler, .desc = "Defines the return value of compiler.get_argument_syntax().");
	doc(check_ignored_option,
		compiler,
		.desc
		= "Called for compiler.has_*_argument() with the compiler's output."
		  " Useful if the compiler exits 0 but indicates on stderr or stdout that an option was ignored.");
	doc(color_output, compiler, .desc = "`-fdiagnostics-color`");
	doc(compile_only, compiler, .desc = "`-c`");
	doc(coverage, compiler, .desc = "`--coverage`");
	doc(crt, compiler, .desc = "Determine the C-runtime, e.g. for msvc.");
	doc(debug, compiler, .desc = "`-g`");
	doc(debugfile, compiler, .desc = "Used to tell msvc where to put pdb files.");
	doc(define, compiler, .desc = "`-D`");
	doc(deps, compiler, .desc = "Arguments that instruct the compiler to output extra dependencies such as headers.");
	doc(deps_type, compiler, .desc = "Determines deps type that ninja to uses");
	doc(do_archiver_passthrough,
		compiler,
		.desc
		= "`true` if the archiver is the same executable as the compiler (e.g. tcc)");
	doc(do_linker_passthrough,
		compiler,
		.desc
		= "`true` if the linker should only be invoked through the compiler driver, or `false` if the linker should be invoked directly.");
	doc(dumpmachine, compiler, .desc = "Argument to output the compiler's target triple.");
	doc(emit_pch, compiler, .desc = "");
	doc(lto_supported, compiler, .desc = "Whether this compiler supports LTO compilation and linking.");
	doc(lto_compile, compiler,
		.desc = "Link-time optimization compile arguments."
		" Receives the thread count as a string; `0` means automatic.");
	doc(lto_link, compiler,
		.desc = "Link-time optimization link arguments."
		" Receives the thread count as a string; `0` means automatic.");
	doc(force_language, compiler, .desc = "`-x`");
	doc(include, compiler, .desc = "`-I`");
	doc(include_dirafter, compiler, .desc = "`-idirafter`");
	doc(include_pch, compiler, .desc = "");
	doc(include_system, compiler, .desc = "`-isystem`");
	doc(linker_delimiter, compiler, .desc = "`/link`");
	doc(linker_passthrough, compiler, .desc = "Handles properly using `-Wl,` to pass arguments to the linker.");
	doc(object_ext, compiler, .desc = "e.g. `.o` or `.obj`");
	doc(optimization, compiler, .desc = "", .enum_arg = "enum compiler_optimization_lvl");
	doc(output, compiler, .desc = "`-o`");
	doc(pch_ext, compiler, .desc = "");
	doc(permissive, compiler, .desc = "`-fpermissive`");
	doc(pgo, compiler, .desc = "", .enum_arg = "enum compiler_pgo_stage");
	doc(pic, compiler, .desc = "`-fPIC`");
	doc(pie, compiler, .desc = "`-fPIE`");
	doc(preprocess_only, compiler, .desc = "`-E`");
	doc(print_search_dirs, compiler, .desc = "Instruct the compilerArgument to output the compiler's internal search directories.");
	doc(sanitize, compiler, .desc = "`-fsanitize`");
	doc(set_std, compiler, .desc = "`-std`");
	doc(std_unsupported, compiler, .desc = "Return true if the passed in std is unsupported by the current compiler.");
	doc(version, compiler, .desc = "`--version`");
	doc(visibility, compiler, .desc = "", .enum_arg = "enum compiler_visibility_type");
	doc(warn_everything, compiler, .desc = "`-Weverything`");
	doc(warning_lvl, compiler, .desc = "", .enum_arg = "enum compiler_warning_lvl");
	doc(werror, compiler, .desc = "`-Werror`");
	doc(winvalid_pch, compiler, .desc = "");
	doc(asan_lundef_warning, compiler, .desc = "whether to warn if b_lundef is used alongside sanitizers");

	// linker
	doc(allow_shlib_undefined, linker, .desc = "Allow undefined symbols");
	doc(always,
		linker,
		.desc = "Arguments that are almost always passed, except in special cases such as detection.");
	doc(as_needed, linker, .desc = "");
	doc(check_ignored_option,
		linker,
		.desc = "Called for compiler.has_link_argument() with the linker's output."
			" Useful if the linker exits 0 but indicates on stderr or stdout that an option was ignored.");
	doc(coverage, linker, .desc = "`--coverage`");
	doc(debug, linker, .desc = "`/DEBUG`");
	doc(def, linker, .desc = "`/DEF`");
	doc(end_group, linker, .desc = "`--end-group`");
	doc(export_dynamic, linker, .desc = "`-export-dynamic`");
	doc(fatal_warnings, linker, .desc = "`--fatal-warnings`");
	doc(fuse_ld, linker, .desc = "`-fuse-ld=`");
	doc(implib, linker, .desc = "`/IMPLIB`");
	doc(implib_suffix, linker, .desc = "Linker specific import library suffix for DLLs. Defaults to '-implib.lib' if not provided.");
	doc(input_output, linker, .desc = "Passed in `input` and `output` and orders them properly, optionally adding additional flags.");
	doc(lib, linker, .desc = "`-l`");
	doc(to_native, linker, .desc = "Convert arguments to linker-specific native format.");
	doc(no_undefined, linker, .desc = "`--no-undefined`");
	doc(pgo, linker, .desc = "", .enum_arg = "enum compiler_pgo_stage");
	doc(process_rpath, linker, .desc = "process rpath directories");
	doc(rpath, linker, .desc = "`-rpath`");
	doc(sanitize, linker, .desc = "`-fsanitize`");
	doc(shared, linker, .desc = "`-shared`");
	doc(shared_module, linker, .desc = "");
	doc(soname, linker, .desc = "`-soname`");
	doc(start_group, linker, .desc = "`--start-group`");
	doc(version, linker, .desc = "`-v`");
	doc(whole_archive, linker, .desc = "`--whole-archive`");

	// archiver
	doc(always, archiver, .desc = "Arguments that are almost always passed, except in special cases such as detection.");
	doc(base, archiver, .desc = "Arguments used to create an archive, e.g. `csr` for ar-posix.");
	doc(input_output, archiver, .desc = "Passed in `input` and `output` and orders them properly, optionally adding additional flags.");
	doc(needs_wipe, archiver, .desc = "`true` if this archiver cannot overwrite old archives and needs muon to remove them for it.");
	doc(version, archiver, .desc = "`--version`");

#undef doc

	for (uint32_t c = 0; c < toolchain_component_count; ++c) {
		handler_array_info_init(wk, toolchain_arg_handlers[c].handlers, toolchain_arg_handlers[c].len);
	}
}

static void
language_snippet_info_init(struct workspace *wk)
{
#define doc(name_, ...) \
	language_snippet_info_doc(#name_, &(struct toolchain_handler_info){ __VA_ARGS__ })
	doc(alignment, .desc = "alignment");
	doc(attribute, .desc = "attribute");
	doc(check_header, .desc = "check_header");
	doc(get_define, .desc = "compiler_get_define");
	doc(compute_int, .desc = "compute_int");
	doc(has_builtin_function, .desc = "has_builtin_function");
	doc(has_function, .desc = "has_function");
	doc(has_header_symbol, .desc = "has_header_symbol");
	doc(has_member, .desc = "has_member");
	doc(has_type, .desc = "has_type");
	doc(minimal_exe, .desc = "minimal_exe");
	doc(sizeof, .desc = "sizeof");

#undef doc

	handler_array_info_init(wk, language_snippet_handler_info, ARRAY_LEN(language_snippet_handler_info));
}

static void
handler_array_info_doc(struct workspace *wk,
	struct toolchain_handler_info *arr,
	const uint32_t len,
	struct tstr *buf)
{
	tstr_pushs(wk, buf,
			"|name|signature|description|\n"
			"|---|---|---|\n");


	for (uint32_t i = 0; i < len; ++i) {
		struct toolchain_handler_info *handler = &arr[i];

		TSTR(sig);
		typecheck_closure_type_to_s(wk, &sig, &handler->sig);

		tstr_pushf(wk, buf, "%s|`", handler->name);
		for (uint32_t i = 0; i < sig.len; ++i) {
			if (sig.buf[i] == '|') {
				tstr_pushs(wk, buf, " / ");
			} else {
				tstr_push(wk, buf, sig.buf[i]);
			}
		}
		tstr_pushf(wk, buf, "`|%s\n", handler->desc);
	}
}

void
toolchain_overrides_doc(struct workspace *wk, enum toolchain_component c, struct tstr *buf)
{
	handler_array_info_doc(wk, toolchain_arg_handlers[c].handlers, toolchain_arg_handlers[c].len, buf);
}

void
language_snippets_doc(struct workspace *wk, struct tstr *buf)
{
	handler_array_info_doc(wk, language_snippet_handler_info, ARRAY_LEN(language_snippet_handler_info), buf);
}

static bool
handler_validate(struct workspace *wk,
	uint32_t ip,
	obj v,
	const struct str *name,
	const struct toolchain_handler_info *handler)
{
	if (get_obj_type(wk, v) == obj_closure) {
		if (!typecheck_closure(wk, ip, v, &handler->sig, name->s)) {
			return false;
		}

		if (handler->enum_arg) {
			struct obj_closure *closure = get_obj_closure(wk, v);
			closure->func->an[1].type = handler->an[1].type;

			if (wk->vm.in_analyzer) {
				az_analyze_func(wk, v);
			}
		}
	} else if (!typecheck_custom(wk, 0, v, handler->sig.return_type, 0)) {
		vm_error_at(wk,
			ip,
			"expected value type %s for handler '%s' with constant return value, got %s\n",
			typechecking_type_to_s(wk, handler->sig.return_type),
			name->s,
			obj_typestr(wk, v));
		return false;
	}

	return true;
}

bool
toolchain_overrides_validate(struct workspace *wk, uint32_t ip, obj handlers, enum toolchain_component component)
{
	obj k, v;
	obj_dict_for(wk, handlers, k, v) {
		const struct str *name = get_str(wk, k);
		const struct toolchain_handler_info *handler;
		if (!(handler = toolchain_handler_info_get(component, name->s))) {
			vm_error_at(wk, ip, "unknown toolchain function %o", k);
			return false;
		}

		if (!handler_validate(wk, ip, v, name, handler)) {
			return false;
		}
	}

	return true;
}

bool
language_snippets_validate(struct workspace *wk, uint32_t ip, obj handlers)
{
	obj k, v;
	obj_dict_for(wk, handlers, k, v) {
		const struct str *name = get_str(wk, k);
		const struct toolchain_handler_info *handler;
		if (!(handler = language_snippet_info_get(name->s))) {
			vm_error_at(wk, ip, "unknown language snippet %o", k);
			return false;
		}

		if (!handler_validate(wk, ip, v, name, handler)) {
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

static obj
lookup_language_snippet(struct workspace *wk, const struct compiler_language l, uint32_t arg)
{
	const struct language *lang = toolchain_language_get(wk, l);
	obj snippet;
	if (lang->snippets
		&& obj_dict_index_str(wk, lang->snippets, language_snippet_handler_info[arg].name, &snippet)) {
		return snippet;
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

enum language_snippet_arg {
#define TOOLCHAIN_ARG_MEMBER(name, type) language_snippet_arg_##name,
	FOREACH_LANGUAGE_SNIPPET(TOOLCHAIN_ARG_MEMBER)
#undef TOOLCHAIN_ARG_MEMBER
};

static obj handle_toolchain_arg_override;

static obj
handle_toolchain_arg_override_returning_args(struct workspace *wk, struct args_norm *an, struct args_kw *akw)
{
	obj list = 0;
	if (get_obj_type(wk, handle_toolchain_arg_override) == obj_array) {
		list = handle_toolchain_arg_override;
	} else {
		if (!vm_eval_closure(wk, handle_toolchain_arg_override, an, akw, &list)) {
			UNREACHABLE;
		}
	}

	return list;
}

static obj
handle_toolchain_arg_override_returning_str(struct workspace *wk, struct args_norm *an, struct args_kw *akw)
{
	obj str = 0;
	if (get_obj_type(wk, handle_toolchain_arg_override) != obj_closure) {
		str = handle_toolchain_arg_override;
	} else {
		if (!vm_eval_closure(wk, handle_toolchain_arg_override, an, akw, &str)) {
			UNREACHABLE;
		}
	}

	return str;
}

static bool
handle_toolchain_arg_override_returning_bool(struct workspace *wk, struct args_norm *an)
{
	if (get_obj_type(wk, handle_toolchain_arg_override) == obj_bool) {
		return get_obj_bool(wk, handle_toolchain_arg_override);
	}

	obj b;
	if (!vm_eval_closure(wk, handle_toolchain_arg_override, an, 0, &b)) {
		UNREACHABLE;
	}

	return get_obj_bool(wk, b);
}

static obj
handle_toolchain_arg_override_0(TOOLCHAIN_SIG_0)
{
	struct args_norm an[] = { { .val = comp }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an, 0);
}

static obj
handle_toolchain_arg_override_0rs(TOOLCHAIN_SIG_0rs)
{
	struct args_norm an[] = { { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_str(wk, an, akw);
}

static obj
handle_toolchain_arg_override_1i(TOOLCHAIN_SIG_1i)
{
	struct args_norm an[] = { { .val = comp }, { .val = i1 }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an, 0);
}

static obj
handle_toolchain_arg_override_1n(TOOLCHAIN_SIG_1n)
{
	struct args_norm an[] = { { .val = comp }, { .val = n1 }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an, 0);
}

static obj
handle_toolchain_arg_override_1s(TOOLCHAIN_SIG_1s)
{
	struct args_norm an[] = { { .val = comp }, { .val = make_str(wk, s1) }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an, 0);
}

static obj
handle_toolchain_arg_override_1srs(TOOLCHAIN_SIG_1srs)
{
	struct args_norm an[] = { { .val = s1 }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_str(wk, an, akw);
}

static obj
handle_toolchain_arg_override_2s(TOOLCHAIN_SIG_2s)
{
	struct args_norm an[] = { { .val = comp }, { .val = make_str(wk, s1) }, { .val = make_str(wk, s2) }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an, 0);
}

static obj
handle_toolchain_arg_override_2srs(TOOLCHAIN_SIG_2srs)
{
	struct args_norm an[] = { { .val = s1 }, { .val = s2 }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_str(wk, an, akw);
}

static obj
handle_toolchain_arg_override_1s1b(TOOLCHAIN_SIG_1s1b)
{
	struct args_norm an[]
		= { { .val = comp }, { .val = make_str(wk, s1) }, { .val = b1 ? obj_bool_true : obj_bool_false }, { ARG_TYPE_NULL }, };
	return handle_toolchain_arg_override_returning_args(wk, an, 0);
}

static obj
handle_toolchain_arg_override_ns(TOOLCHAIN_SIG_ns)
{
	struct args_norm an[] = { { .val = comp }, { .val = ns1 }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an, 0);
}

static obj
handle_toolchain_arg_override_kw(TOOLCHAIN_SIG_kw)
{
	struct args_norm an[] = { { .val = comp }, { ARG_TYPE_NULL } };
	return handle_toolchain_arg_override_returning_args(wk, an, akw);
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
toolchain_print_dumped_args(struct workspace *wk, obj args)
{
	obj_printf(wk, "%o\n", args);
}

static void
toolchain_print_dumped_bool(bool v)
{
	printf("%s\n", v ? "true" : "false");
}

static void
toolchain_dump_args_0(struct workspace *wk, obj args)
{
	toolchain_print_dumped_args(wk, args);
}

static void
toolchain_dump_args_1i(struct workspace *wk, obj args)
{
	toolchain_print_dumped_args(wk, args);
}

static void
toolchain_dump_args_1n(struct workspace *wk, obj args)
{
	toolchain_print_dumped_args(wk, args);
}

static void
toolchain_dump_args_1s(struct workspace *wk, obj args)
{
	toolchain_print_dumped_args(wk, args);
}

static void
toolchain_dump_args_2s(struct workspace *wk, obj args)
{
	toolchain_print_dumped_args(wk, args);
}

static void
toolchain_dump_args_1s1b(struct workspace *wk, obj args)
{
	toolchain_print_dumped_args(wk, args);
}

static void
toolchain_dump_args_ns(struct workspace *wk, obj args)
{
	toolchain_print_dumped_args(wk, args);
}

static void
toolchain_dump_args_kw(struct workspace *wk, obj args)
{
	toolchain_print_dumped_args(wk, args);
}

static void
toolchain_dump_args_0rb(struct workspace *wk, bool v)
{
	toolchain_print_dumped_bool(v);
}

static void
toolchain_dump_args_1srb(struct workspace *wk, bool v)
{
	toolchain_print_dumped_bool(v);
}

void
toolchain_dump(struct workspace *wk, obj comp, struct toolchain_dump_opts *opts)
{
	const char *s1 = opts->s1, *s2 = opts->s2;
	const bool b1 = opts->b1;
	const uint32_t i1 = opts->i1;
	obj n1 = make_number(wk, opts->n1);
	obj ns1 = opts->ns1;
	struct args_kw akw[] = { 0 };

	printf("%-13s %-25s %-4s %s\n", "component", "name", "sig", "args");
	printf("%-13s %-25s %-4s %s\n", "---", "---", "---", "---");

#define TOOLCHAIN_ARG_MEMBER_(name, _name, component, return_type, _type, params, names) \
	printf("%-13s %-25s %-4s ", #component, #name, #_type);                          \
	toolchain_dump_args_##_type(wk, toolchain_##component##_name names);
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, _##name, comp, type)

	FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER)
	FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
	FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)

#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_
}

#define TOOLCHAIN_ARG_MEMBER_(name, return_type, _type, params, names)                           \
	return_type language_snippet_##name params                                                            \
	{                                                                                                          \
		handle_toolchain_arg_override = lookup_language_snippet(                                     \
			wk, l, language_snippet_arg_##name); \
		if (handle_toolchain_arg_override) {                                                               \
			return handle_toolchain_arg_override_##_type names;                                        \
		}                                                                                                  \
		return toolchain_arg_empty_ ##_type names;                                                                    \
	}

#define TOOLCHAIN_ARG_MEMBER(name, type) TOOLCHAIN_ARG_MEMBER_(name, type)

FOREACH_LANGUAGE_SNIPPET(TOOLCHAIN_ARG_MEMBER)

#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

const char *
toolchain_compiler_flatten_one(struct workspace *wk, obj comp_id, obj args)
{
	obj e;
	if (obj_array_flatten_one(wk, args, &e)) {
		return get_cstr(wk, e);
	} else {
		const char *id = toolchain_component_type_to_id(wk,
			toolchain_component_compiler,
			get_obj_compiler(wk, comp_id)->type[toolchain_component_compiler])
					 ->public_id;
		vm_error(wk,
			"handler for compiler %s returned an invalid value, "
			"an array containing a single string was expected",
			id);
		return 0;
	}
}

const char *
toolchain_compiler_flatten_one_optional(struct workspace *wk, obj args)
{
	obj e;
	if (obj_array_flatten_one(wk, args, &e)) {
		return get_cstr(wk, e);
	} else {
		return 0;
	}
}

const char *
compiler_log_prefix(struct workspace *wk, struct compiler_language lang, enum machine_kind machine)
{
	static char buf[256];

	if (machine == machine_kind_build) {
		snprintf(buf,
			sizeof(buf),
			"%s %s machine compiler",
			compiler_language_to_s(wk, lang),
			machine_kind_to_s(machine));
	} else {
		snprintf(buf, sizeof(buf), "%s compiler", compiler_language_to_s(wk, lang));
	}

	return buf;
}

void
toolchains_init(struct workspace *wk)
{
	arr_init(wk->a, &wk->language_registry.languages, 16, struct language);

	{
		struct language *lang = arr_grow_by(wk->a, &wk->language_registry.languages, 1);
		lang->name = STR("null");
	}

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

	vm_enum(wk, enum toolchain_component);
	vm_enum_value_prefixed(wk, toolchain_component, compiler);
	vm_enum_value_prefixed(wk, toolchain_component, linker);
	vm_enum_value_prefixed(wk, toolchain_component, archiver);

	toolchain_handler_info_init(wk);
	language_snippet_info_init(wk);
}

