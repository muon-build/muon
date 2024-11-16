/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Owen Rafferty <owen@owenrafferty.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "backend/output.h"
#include "error.h"
#include "functions/dependency.h"
#include "lang/object_iterators.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static void
get_option_value_for_tgt(struct workspace *wk,
	const struct project *proj,
	const struct obj_build_target *tgt,
	const char *name,
	obj *res)
{
	get_option_value_overridable(wk, proj, tgt ? tgt->override_options : 0, name, res);
}

struct buildtype {
	enum compiler_optimization_lvl opt;
	bool debug;
};

static void
get_buildtype(struct workspace *wk,
	const struct project *proj,
	const struct obj_build_target *tgt,
	struct buildtype *buildtype)
{
	uint32_t i;
	buildtype->opt = 0;
	buildtype->debug = false;

	static struct {
		const char *name;
		enum compiler_optimization_lvl opt;
		bool debug;
	} tbl[] = { { "plain", compiler_optimization_lvl_none, false },
		{ "debug", compiler_optimization_lvl_0, true },
		{ "debugoptimized", compiler_optimization_lvl_g, true },
		{ "release", compiler_optimization_lvl_3, false },
		{ "minsize", compiler_optimization_lvl_s, false },
		{ NULL } };

	obj buildtype_opt_id, buildtype_val;
	get_option_overridable(wk, proj, tgt ? tgt->override_options : 0, &WKSTR("buildtype"), &buildtype_opt_id);
	struct obj_option *buildtype_opt = get_obj_option(wk, buildtype_opt_id);
	buildtype_val = buildtype_opt->val;

	const char *str = get_cstr(wk, buildtype_val);

	bool use_custom = (strcmp(str, "custom") == 0) || (buildtype_opt->source <= option_value_source_default);

	if (use_custom) {
		obj optimization_id, debug_id;

		get_option_value_for_tgt(wk, proj, tgt, "optimization", &optimization_id);
		get_option_value_for_tgt(wk, proj, tgt, "debug", &debug_id);

		const struct str *str = get_str(wk, optimization_id);
		if (str_eql(str, &WKSTR("plain"))) {
			buildtype->opt = compiler_optimization_lvl_none;
		} else if (str->len != 1) {
			UNREACHABLE;
		}

		switch (*str->s) {
		case '0':
		case '1':
		case '2':
		case '3': buildtype->opt = compiler_optimization_lvl_0 + (*str->s - '0'); break;
		case 'g': buildtype->opt = compiler_optimization_lvl_g; break;
		case 's': buildtype->opt = compiler_optimization_lvl_s; break;
		default: UNREACHABLE;
		}

		buildtype->debug = get_obj_bool(wk, debug_id);
	} else {
		for (i = 0; tbl[i].name; ++i) {
			if (strcmp(str, tbl[i].name) == 0) {
				buildtype->opt = tbl[i].opt;
				buildtype->debug = tbl[i].debug;
				break;
			}
		}

		if (!tbl[i].name) {
			LOG_E("invalid build type %s", str);
			UNREACHABLE;
		}
	}
}

static void
get_buildtype_args(struct workspace *wk, struct obj_compiler *comp, const struct buildtype *buildtype, obj args_id)
{
	if (buildtype->debug) {
		push_args(wk, args_id, toolchain_compiler_debug(wk, comp));
	}

	push_args(wk, args_id, toolchain_compiler_optimization(wk, comp, buildtype->opt));
}

static void
get_warning_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj lvl_id;
	get_option_value_for_tgt(wk, proj, tgt, "warning_level", &lvl_id);
	const struct str *sl = get_str(wk, lvl_id);

	if (str_eql(sl, &WKSTR("everything"))) {
		push_args(wk, args_id, toolchain_compiler_warn_everything(wk, comp));
		return;
	}

	uint32_t lvl;
	assert(sl->len == 1 && "invalid warning_level");
	switch (sl->s[0]) {
	case '0': lvl = 0; break;
	case '1': lvl = 1; break;
	case '2': lvl = 2; break;
	case '3': lvl = 3; break;
	default: UNREACHABLE; return;
	}

	push_args(wk, args_id, toolchain_compiler_warning_lvl(wk, comp, lvl));
}

static void
get_werror_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj active;
	get_option_value_for_tgt(wk, proj, tgt, "werror", &active);

	if (get_obj_bool(wk, active)) {
		push_args(wk, args_id, toolchain_compiler_werror(wk, comp));
	}
}

void
get_std_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj std;

	switch (comp->lang) {
	case compiler_language_c: get_option_value_for_tgt(wk, proj, tgt, "c_std", &std); break;
	case compiler_language_objcpp:
	case compiler_language_cpp: get_option_value_for_tgt(wk, proj, tgt, "cpp_std", &std); break;
	default: return;
	}

	const char *s, *next = get_cstr(wk, std);
	char buf[256];
	uint32_t len;

	do {
		s = next;
		if ((next = strchr(s, ','))) {
			len = next - s;
			++next;
		} else {
			len = strlen(s);
			next = s + len;
		}

		if (!len) {
			continue;
		} else if (len >= sizeof(buf)) {
			LOG_W("skipping invalid std '%.*s'", len, s);
		} else if (strncmp(s, "none", len) == 0) {
			return;
		} else {
			memcpy(buf, s, len);
			buf[len] = 0;

			if (toolchain_compiler_std_supported(wk, comp, buf)) {
				push_args(wk, args_id, toolchain_compiler_set_std(wk, comp, buf));
				return;
			}
		}
	} while (*next);

	LOG_W("none of the requested stds are supported: '%s'", get_cstr(wk, std));
}

void
get_option_compile_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj args;
	switch (comp->lang) {
	case compiler_language_c: get_option_value_for_tgt(wk, proj, tgt, "c_args", &args); break;
	case compiler_language_cpp: get_option_value_for_tgt(wk, proj, tgt, "cpp_args", &args); break;
	default: return;
	}

	obj_array_extend(wk, args_id, args);
}

struct setup_compiler_args_ctx {
	const struct obj_build_target *tgt;
	const struct project *proj;

	obj include_dirs;
	obj dep_args;
	obj joined_args;
};

static void
setup_optional_b_args_compiler(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	const struct buildtype *buildtype,
	obj args)
{
	obj opt;

	get_option_value_for_tgt(wk, proj, tgt, "b_vscrt", &opt);
	push_args(wk, args, toolchain_compiler_crt(wk, comp, get_cstr(wk, opt), buildtype->debug));

	get_option_value_for_tgt(wk, proj, tgt, "b_pgo", &opt);
	if (!str_eql(get_str(wk, opt), &WKSTR("off"))) {
		uint32_t stage;
		const struct str *sl = get_str(wk, opt);
		if (str_eql(sl, &WKSTR("generate"))) {
			stage = 0;
		} else if (str_eql(sl, &WKSTR("use"))) {
			stage = 1;
		} else {
			UNREACHABLE;
			return;
		}
		push_args(wk, args, toolchain_compiler_pgo(wk, comp, stage));
	}

	get_option_value_for_tgt(wk, proj, tgt, "b_sanitize", &opt);
	if (!str_eql(get_str(wk, opt), &WKSTR("none"))) {
		push_args(wk, args, toolchain_compiler_sanitize(wk, comp, get_cstr(wk, opt)));
	}

	obj buildtype_val;
	get_option_value_for_tgt(wk, proj, tgt, "buildtype", &buildtype_val);
	get_option_value_for_tgt(wk, proj, tgt, "b_ndebug", &opt);
	if (str_eql(get_str(wk, opt), &WKSTR("true"))
		|| (str_eql(get_str(wk, opt), &WKSTR("if-release"))
			&& str_eql(get_str(wk, buildtype_val), &WKSTR("release")))) {
		push_args(wk, args, toolchain_compiler_define(wk, comp, "NDEBUG"));
	}

	get_option_value_for_tgt(wk, proj, tgt, "b_colorout", &opt);
	if (!str_eql(get_str(wk, opt), &WKSTR("never"))) {
		push_args(wk, args, toolchain_compiler_color_output(wk, comp, get_cstr(wk, opt)));
	}

	get_option_value_for_tgt(wk, proj, tgt, "b_lto", &opt);
	if (get_obj_bool(wk, opt)) {
		push_args(wk, args, toolchain_compiler_enable_lto(wk, comp));
	}

	get_option_value_for_tgt(wk, proj, tgt, "b_coverage", &opt);
	if (get_obj_bool(wk, opt)) {
		push_args(wk, args, toolchain_compiler_coverage(wk, comp));
	}
}

static bool
get_base_compiler_args(struct workspace *wk,
	const struct project *proj,
	const struct obj_build_target *tgt,
	enum compiler_language lang,
	obj comp_id,
	obj *res)
{
	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	struct buildtype buildtype;

	get_buildtype(wk, proj, tgt, &buildtype);

	obj args;
	make_obj(wk, &args, obj_array);

	push_args(wk, args, toolchain_compiler_always(wk, comp));

	get_std_args(wk, comp, proj, tgt, args);
	get_buildtype_args(wk, comp, &buildtype, args);
	get_warning_args(wk, comp, proj, tgt, args);
	get_werror_args(wk, comp, proj, tgt, args);

	setup_optional_b_args_compiler(wk, comp, proj, tgt, &buildtype, args);

	{ /* option args (from option('x_args')) */
		get_option_compile_args(wk, comp, proj, tgt, args);
	}

	{ /* global args */
		obj global_args;
		if (obj_dict_geti(wk, wk->global_args[tgt->machine], lang, &global_args)) {
			obj_array_extend(wk, args, global_args);
		}
	}

	{ /* project args */
		obj proj_args;
		if (obj_dict_geti(wk, proj->args[tgt->machine], lang, &proj_args)) {
			obj_array_extend(wk, args, proj_args);
		}
	}

	*res = args;
	return true;
}

void
setup_compiler_args_includes(struct workspace *wk, obj compiler, obj include_dirs, obj args, bool relativize)
{
	struct obj_compiler *comp = get_obj_compiler(wk, compiler);

	obj v;
	obj_array_for(wk, include_dirs, v) {
		const char *dir;
		bool is_system;
		{
			enum obj_type t = get_obj_type(wk, v);
			switch (t) {
			case obj_include_directory: {
				struct obj_include_directory *inc = get_obj_include_directory(wk, v);
				dir = get_cstr(wk, inc->path);
				is_system = inc->is_system;
				break;
			}
			case obj_string:
				dir = get_cstr(wk, v);
				is_system = false;
				break;
			default: LOG_E("invalid type for include directory '%s'", obj_type_to_s(t)); UNREACHABLE;
			}
		}

		SBUF(rel);
		if (relativize) {
			if (!fs_dir_exists(dir)) {
				continue;
			}

			if (path_is_absolute(dir)) {
				path_relative_to(wk, &rel, wk->build_root, dir);
				dir = rel.buf;
			}
		}

		if (is_system) {
			push_args(wk, args, toolchain_compiler_include_system(wk, comp, dir));
		} else {
			push_args(wk, args, toolchain_compiler_include(wk, comp, dir));
		}
	};
}

static enum iteration_result
setup_compiler_args_iter(struct workspace *wk, void *_ctx, obj k, obj comp_id)
{
	enum compiler_language lang = k;
	struct setup_compiler_args_ctx *ctx = _ctx;

	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);

	obj args;
	if (!get_base_compiler_args(wk, ctx->proj, ctx->tgt, lang, comp_id, &args)) {
		return ir_err;
	}

	obj inc_dirs;
	obj_array_dedup(wk, ctx->include_dirs, &inc_dirs);

	{ /* project includes */
		obj proj_incs;
		if (obj_dict_geti(wk, ctx->proj->include_dirs[ctx->tgt->machine], lang, &proj_incs)) {
			obj_array_extend(wk, inc_dirs, proj_incs);
			obj dedupd;
			obj_array_dedup(wk, inc_dirs, &dedupd);
			inc_dirs = dedupd;
		}
	}

	setup_compiler_args_includes(wk, comp_id, inc_dirs, args, true);

	{ /* dep args */
		if (ctx->dep_args) {
			obj_array_extend(wk, args, ctx->dep_args);
		}
	}

	{ /* target args */
		obj tgt_args;
		if (obj_dict_geti(wk, ctx->tgt->args, lang, &tgt_args) && tgt_args
			&& get_obj_array(wk, tgt_args)->len) {
			obj_array_extend(wk, args, tgt_args);
		}
	}

	if (ctx->tgt->flags & build_tgt_flag_pic) {
		push_args(wk, args, toolchain_compiler_pic(wk, comp));
	}

	if (ctx->tgt->flags & build_tgt_flag_pie) {
		push_args(wk, args, toolchain_compiler_pie(wk, comp));
	}

	if (ctx->tgt->flags & build_tgt_flag_visibility) {
		push_args(wk, args, toolchain_compiler_visibility(wk, comp, ctx->tgt->visibility));
	}

	obj_dict_seti(wk, ctx->joined_args, lang, join_args_shell_ninja(wk, args));
	return ir_cont;
}

bool
setup_compiler_args(struct workspace *wk,
	const struct obj_build_target *tgt,
	const struct project *proj,
	obj include_dirs,
	obj dep_args,
	obj *joined_args)
{
	make_obj(wk, joined_args, obj_dict);

	struct setup_compiler_args_ctx ctx = {
		.tgt = tgt,
		.proj = proj,
		.include_dirs = include_dirs,
		.dep_args = dep_args,
		.joined_args = *joined_args,
	};

	if (!obj_dict_foreach(wk, proj->toolchains[tgt->machine], &ctx, setup_compiler_args_iter)) {
		return false;
	}

	return true;
}

bool
build_target_args(struct workspace *wk, const struct project *proj, const struct obj_build_target *tgt, obj *joined_args)
{
	struct build_dep args = tgt->dep_internal;

	if (tgt->flags & build_tgt_generated_include) {
		const char *private_path = get_cstr(wk, tgt->private_path);

		// mkdir so that the include dir doesn't get pruned later on
		if (!fs_mkdir_p(private_path)) {
			return false;
		}

		obj inc;
		make_obj(wk, &inc, obj_array);
		obj_array_push(wk, inc, make_str(wk, private_path));
		obj_array_extend_nodup(wk, inc, args.include_directories);
		args.include_directories = inc;
	}

	if (!setup_compiler_args(wk, tgt, proj, args.include_directories, args.compile_args, joined_args)) {
		return false;
	}

	return true;
}

void
get_option_link_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj args;
	switch (comp->lang) {
	case compiler_language_c: get_option_value_for_tgt(wk, proj, tgt, "c_link_args", &args); break;
	case compiler_language_cpp: get_option_value_for_tgt(wk, proj, tgt, "cpp_link_args", &args); break;
	default: return;
	}

	obj_array_extend(wk, args_id, args);
}

static void
push_linker_args(struct workspace *wk, struct setup_linker_args_ctx *ctx, const struct args *args)
{
	if (!args->len) {
		return;
	}

	if (toolchain_compiler_do_linker_passthrough(wk, ctx->compiler)) {
		args = toolchain_compiler_linker_passthrough(wk, ctx->compiler, args);
	}

	push_args(wk, ctx->args->link_args, args);
}

static enum iteration_result
process_rpath_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct setup_linker_args_ctx *ctx = _ctx;

	if (!get_str(wk, v)->len) {
		return ir_cont;
	}

	push_linker_args(wk, ctx, toolchain_linker_rpath(wk, ctx->compiler, get_cstr(wk, v)));

	return ir_cont;
}

static bool
setup_optional_b_args_linker(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args)
{
	obj opt;
	get_option_value_for_tgt(wk, proj, tgt, "b_pgo", &opt);
	if (!str_eql(get_str(wk, opt), &WKSTR("off"))) {
		uint32_t stage;
		const struct str *sl = get_str(wk, opt);
		if (str_eql(sl, &WKSTR("generate"))) {
			stage = 0;
		} else if (str_eql(sl, &WKSTR("use"))) {
			stage = 1;
		} else {
			UNREACHABLE;
			return false;
		}
		push_args(wk, args, toolchain_linker_pgo(wk, comp, stage));
	}

	get_option_value_for_tgt(wk, proj, tgt, "b_sanitize", &opt);
	if (strcmp(get_cstr(wk, opt), "none") != 0) {
		push_args(wk, args, toolchain_linker_sanitize(wk, comp, get_cstr(wk, opt)));
	}

	get_option_value_for_tgt(wk, proj, tgt, "b_lto", &opt);
	if (get_obj_bool(wk, opt)) {
		push_args(wk, args, toolchain_linker_enable_lto(wk, comp));
	}

	get_option_value_for_tgt(wk, proj, tgt, "b_coverage", &opt);
	if (get_obj_bool(wk, opt)) {
		push_args(wk, args, toolchain_linker_coverage(wk, comp));
	}

	return true;
}

static enum iteration_result
push_not_found_lib_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct setup_linker_args_ctx *ctx = _ctx;

	push_linker_args(wk, ctx, toolchain_linker_lib(wk, ctx->compiler, get_cstr(wk, v)));
	return ir_cont;
}

void
setup_linker_args(struct workspace *wk,
	const struct project *proj,
	const struct obj_build_target *tgt,
	struct setup_linker_args_ctx *ctx)
{
	ctx->proj = proj;
	ctx->tgt = tgt;

	obj link_with;
	obj_array_dedup(wk, ctx->args->link_with, &link_with);
	ctx->args->link_with = link_with;

	obj link_whole;
	obj_array_dedup(wk, ctx->args->link_whole, &link_whole);
	ctx->args->link_whole = link_whole;

	obj link_with_not_found;
	obj_array_dedup(wk, ctx->args->link_with_not_found, &link_with_not_found);
	ctx->args->link_with_not_found = link_with_not_found;

	{
		struct buildtype buildtype;
		get_buildtype(wk, ctx->proj, ctx->tgt, &buildtype);

		if (buildtype.debug) {
			push_linker_args(wk, ctx, toolchain_linker_debug(wk, ctx->compiler));
		}
	}

	push_linker_args(wk, ctx, toolchain_linker_always(wk, ctx->compiler));
	push_linker_args(wk, ctx, toolchain_linker_as_needed(wk, ctx->compiler));

	if (proj) {
		assert(tgt);

		if (!(tgt->type & tgt_shared_module)) {
			push_linker_args(wk, ctx, toolchain_linker_no_undefined(wk, ctx->compiler));
		}

		if (tgt->flags & build_tgt_flag_export_dynamic) {
			push_linker_args(wk, ctx, toolchain_linker_export_dynamic(wk, ctx->compiler));
		}

		setup_optional_b_args_linker(wk, ctx->compiler, proj, tgt, ctx->args->link_args);

		{ /* option args (from option('x_link_args')) */
			get_option_link_args(wk, ctx->compiler, proj, tgt, ctx->args->link_args);
		}

		/* global args */
		obj global_args;
		if (obj_dict_geti(wk, wk->global_link_args[tgt->machine], ctx->compiler->lang, &global_args)) {
			obj_array_extend(wk, ctx->args->link_args, global_args);
		}

		/* project args */
		obj proj_args;
		if (obj_dict_geti(wk, proj->link_args[tgt->machine], ctx->compiler->lang, &proj_args)) {
			obj_array_extend(wk, ctx->args->link_args, proj_args);
		}

		if (tgt && tgt->flags & build_tgt_flag_pic) {
			push_args(wk, ctx->args->link_args, toolchain_compiler_pic(wk, ctx->compiler));
		}
	}

	obj_array_foreach(wk, ctx->args->rpath, ctx, process_rpath_iter);

	if (ctx->args->frameworks) {
		obj v;
		obj_array_for(wk, ctx->args->frameworks, v) {
			obj_array_push(wk, ctx->args->link_args, make_str(wk, "-framework"));
			obj_array_push(wk, ctx->args->link_args, v);
		}
	}

	bool have_link_whole = get_obj_array(wk, ctx->args->link_whole)->len,
	     have_link_with = have_link_whole || get_obj_array(wk, ctx->args->link_with)->len
			      || get_obj_array(wk, ctx->args->link_with_not_found)->len;

	if (have_link_with) {
		push_linker_args(wk, ctx, toolchain_linker_start_group(wk, ctx->compiler));

		if (have_link_whole) {
			obj v;
			obj_array_for(wk, ctx->args->link_whole, v) {
				push_linker_args(
					wk, ctx, toolchain_linker_whole_archive(wk, ctx->compiler, get_cstr(wk, v)));
			}
		}

		if (proj) {
			/* project link_with */
			obj proj_link_with;
			if (obj_dict_geti(wk, proj->link_with[tgt->machine], ctx->compiler->lang, &proj_link_with)) {
				obj_array_extend(wk, ctx->args->link_args, proj_link_with);
			}
		}

		obj_array_extend(wk, ctx->args->link_args, ctx->args->link_with);

		obj_array_foreach(wk, ctx->args->link_with_not_found, ctx, push_not_found_lib_iter);

		push_linker_args(wk, ctx, toolchain_linker_end_group(wk, ctx->compiler));
	}

	if (tgt && (tgt->type & (tgt_dynamic_library | tgt_shared_module))) {
		push_linker_args(wk, ctx, toolchain_linker_soname(wk, ctx->compiler, get_cstr(wk, tgt->soname)));
		if (tgt->type == tgt_shared_module) {
			push_linker_args(wk, ctx, toolchain_linker_allow_shlib_undefined(wk, ctx->compiler));
			push_linker_args(wk, ctx, toolchain_linker_shared_module(wk, ctx->compiler));
		} else {
			push_linker_args(wk, ctx, toolchain_linker_shared(wk, ctx->compiler));
		}
	}
}

/* */

struct relativize_paths_ctx {
	bool relativize_strings;
	obj *oneshot;
	obj dest;
};

static enum iteration_result
relativize_paths_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct relativize_paths_ctx *ctx = _ctx;

	const char *str;

	if (get_obj_type(wk, val) == obj_string) {
		if (ctx->relativize_strings) {
			str = get_cstr(wk, val);
		} else {
			if (ctx->oneshot) {
				*ctx->oneshot = val;
			} else {
				obj_array_push(wk, ctx->dest, val);
			}
			return ir_cont;
		}
	} else {
		str = get_file_path(wk, val);
	}

	SBUF(buf);
	path_relative_to(wk, &buf, wk->build_root, str);
	obj s = sbuf_into_str(wk, &buf);

	if (ctx->oneshot) {
		*ctx->oneshot = s;
	} else {
		obj_array_push(wk, ctx->dest, s);
	}
	return ir_cont;
}

void
relativize_paths(struct workspace *wk, obj arr, bool relativize_strings, obj *res)
{
	make_obj(wk, res, obj_array);
	struct relativize_paths_ctx ctx = {
		.relativize_strings = relativize_strings,
		.dest = *res,
	};

	obj_array_foreach(wk, arr, &ctx, relativize_paths_iter);
}

void
relativize_path(struct workspace *wk, obj path, bool relativize_strings, obj *res)
{
	make_obj(wk, res, obj_array);
	struct relativize_paths_ctx ctx = {
		.relativize_strings = relativize_strings,
		.oneshot = res,
	};

	relativize_paths_iter(wk, &ctx, path);
}

void
relativize_path_push(struct workspace *wk, obj path, obj arr)
{
	struct relativize_paths_ctx ctx = {
		.dest = arr,
	};

	relativize_paths_iter(wk, &ctx, path);
}

static enum iteration_result
add_global_opts_set_from_env_iter(struct workspace *wk, void *_ctx, obj key, obj val)
{
	obj regen_args = *(obj *)_ctx;

	struct obj_option *o = get_obj_option(wk, val);
	if (o->source != option_value_source_environment) {
		return ir_cont;
	}

	// NOTE: This only handles options of type str or [str], which is okay since
	// the only options that can be set from the environment are of this
	// type.
	// TODO: The current implementation of array stringification would
	// choke on spaces, etc.

	const char *sval;
	switch (get_obj_type(wk, o->val)) {
	case obj_string: sval = get_cstr(wk, o->val); break;
	case obj_array: {
		obj joined;
		obj_array_join(wk, true, o->val, make_str(wk, ","), &joined);
		sval = get_cstr(wk, joined);
		break;
	}
	default: UNREACHABLE;
	}

	obj_array_push(wk, regen_args, make_strf(wk, "-D%s=%s", get_cstr(wk, o->name), sval));
	return ir_cont;
}

obj
regenerate_build_command(struct workspace *wk, bool opts_only)
{
	obj regen_args;
	make_obj(wk, &regen_args, obj_array);

	if (!opts_only) {
		obj_array_push(wk, regen_args, make_str(wk, wk->argv0));
		obj_array_push(wk, regen_args, make_str(wk, "-C"));
		obj_array_push(wk, regen_args, make_str(wk, wk->source_root));
		obj_array_push(wk, regen_args, make_str(wk, "setup"));
	}

	obj_dict_foreach(wk, wk->global_opts, &regen_args, add_global_opts_set_from_env_iter);

	uint32_t i;
	for (i = 0; i < wk->original_commandline.argc; ++i) {
		obj_array_push(wk, regen_args, make_str(wk, wk->original_commandline.argv[i]));
	}

	return regen_args;
}
