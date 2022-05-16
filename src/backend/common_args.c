#include "posix.h"

#include <assert.h>
#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "error.h"
#include "functions/dependency.h"
#include "log.h"
/* #include "options.h" */
#include "platform/filesystem.h"
#include "platform/path.h"

void get_option_value_overridable(struct workspace *wk, const struct project *proj, obj overrides, const char *name, obj *res);

static void
get_option_value_for_tgt(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, const char *name, obj *res)
{
	get_option_value_overridable(wk, proj, tgt ? tgt->override_options : 0, name, res);
}

static bool
get_buildtype_args(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, obj args_id, enum compiler_type t)
{
	uint32_t i;
	enum compiler_optimization_lvl opt = 0;
	bool debug = false;

	static struct {
		const char *name;
		enum compiler_optimization_lvl opt;
		bool debug;
	} tbl[] = {
		{ "plain",          compiler_optimization_lvl_0, false },
		{ "debug",          compiler_optimization_lvl_0, true  },
		{ "debugoptimized", compiler_optimization_lvl_g, true  },
		{ "release",        compiler_optimization_lvl_3, false },
		{ "minsize",        compiler_optimization_lvl_s, false },
		{ NULL }
	};

	obj buildtype;
	get_option_value_for_tgt(wk, proj, tgt, "buildtype", &buildtype);

	const char *str = get_cstr(wk, buildtype);

	if (strcmp(str, "custom") == 0) {
		obj optimization_id, debug_id;

		get_option_value_for_tgt(wk, proj, tgt, "optimization", &optimization_id);
		get_option_value_for_tgt(wk, proj, tgt, "debug", &debug_id);

		str = get_cstr(wk, optimization_id);
		switch (*str) {
		case '0': case '1': case '2': case '3':
			opt = compiler_optimization_lvl_0 + (*str - '0');
			break;
		case 'g':
			opt = compiler_optimization_lvl_g;
			break;
		case 's':
			opt = compiler_optimization_lvl_s;
			break;
		default:
			UNREACHABLE;
		}

		debug = get_obj_bool(wk, debug_id);
	} else {
		for (i = 0; tbl[i].name; ++i) {
			if (strcmp(str, tbl[i].name) == 0) {
				opt = tbl[i].opt;
				debug = tbl[i].debug;
				break;
			}
		}

		if (!tbl[i].name) {
			LOG_E("invalid build type %s", str);
			return false;
		}
	}

	if (debug) {
		push_args(wk, args_id, compilers[t].args.debug());
	}

	push_args(wk, args_id, compilers[t].args.optimization(opt));
	return true;
}

static void
get_warning_args(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, obj args_id, enum compiler_type t)
{
	obj lvl_id;
	get_option_value_for_tgt(wk, proj, tgt, "warning_level", &lvl_id);

	uint32_t lvl;
	const struct str *sl = get_str(wk, lvl_id);
	assert(sl->len == 1 && "invalid warning_level");
	switch (sl->s[0]) {
	case '0':
		lvl = 0;
		break;
	case '1':
		lvl = 1;
		break;
	case '2':
		lvl = 2;
		break;
	case '3':
		lvl = 3;
		break;
	default:
		UNREACHABLE;
		return;
	}

	push_args(wk, args_id, compilers[t].args.warning_lvl(lvl));
}

void
get_std_args(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, obj args_id,
	enum compiler_language lang, enum compiler_type t)
{
	obj std;

	switch (lang) {
	case compiler_language_c:
		get_option_value_for_tgt(wk, proj, tgt, "c_std", &std);
		break;
	case compiler_language_cpp:
		get_option_value_for_tgt(wk, proj, tgt, "cpp_std", &std);
		break;
	default:
		return;
	}

	const char *s = get_cstr(wk, std);

	if (strcmp(s, "none") != 0) {
		push_args(wk, args_id, compilers[t].args.set_std(s));
	}
}

void
get_option_compile_args(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, obj args_id, enum compiler_language lang)
{
#ifndef MUON_BOOTSTRAPPED
	// If we aren't bootstrapped, we don't yet have any _args options defined
	return;
#endif

	obj args;
	switch (lang) {
	case compiler_language_c:
		get_option_value_for_tgt(wk, proj, tgt, "c_args", &args);
		break;
	case compiler_language_cpp:
		get_option_value_for_tgt(wk, proj, tgt, "cpp_args", &args);
		break;
	default:
		return;
	}

	obj_array_extend(wk, args_id, args);
}

enum iteration_result
setup_compiler_args_includes(struct workspace *wk, void *_ctx, obj v)
{
	struct setup_compiler_args_includes_ctx *ctx = _ctx;
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
		default:
			LOG_E("invalid type for include directory '%s'", obj_type_to_s(t));
			return false;
		}
	}


	if (!fs_dir_exists(dir)) {
		return ir_cont;
	}

	char rel[PATH_MAX];
	if (path_is_absolute(dir)) {
		if (!path_relative_to(rel, PATH_MAX, wk->build_root, dir)) {
			return ir_err;
		}
		dir = rel;
	}

	if (is_system) {
		push_args(wk, ctx->args, compilers[ctx->t].args.include_system(dir));
	} else {
		push_args(wk, ctx->args, compilers[ctx->t].args.include(dir));
	}
	return ir_cont;
}

struct setup_compiler_args_ctx {
	const struct obj_build_target *tgt;
	const struct project *proj;

	obj include_dirs;
	obj dep_args;
	obj joined_args;
};

static void
setup_optional_b_args_compiler(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, obj args, enum compiler_type t)
{
#ifndef MUON_BOOTSTRAPPED
	// If we aren't bootstrapped, we don't yet have any b_ options defined
	return;
#endif

	obj opt;
	get_option_value_for_tgt(wk, proj, tgt, "b_sanitize", &opt);
	if (!str_eql(get_str(wk, opt), &WKSTR("none"))) {
		push_args(wk, args, compilers[t].args.sanitize(get_cstr(wk, opt)));
	}

	obj buildtype;
	get_option_value_for_tgt(wk, proj, tgt, "buildtype", &buildtype);
	get_option_value_for_tgt(wk, proj, tgt, "b_ndebug", &opt);
	if (str_eql(get_str(wk, opt), &WKSTR("true"))
	    || (str_eql(get_str(wk, opt), &WKSTR("if-release"))
		&& str_eql(get_str(wk, buildtype), &WKSTR("release")))) {
		push_args(wk, args, compilers[t].args.define("NDEBUG"));
	}
}

static bool
get_base_compiler_args(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, enum compiler_language lang,
	obj comp_id, obj *res)
{
	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	enum compiler_type t = comp->type;

	obj args;
	make_obj(wk, &args, obj_array);

	get_std_args(wk, proj, tgt, args, lang, t);
	if (!get_buildtype_args(wk, proj, tgt, args, t)) {
		return false;
	}
	get_warning_args(wk, proj, tgt, args, t);

	setup_optional_b_args_compiler(wk, proj, tgt, args, t);

	{ /* option args (from option('x_args')) */
		get_option_compile_args(wk, proj, tgt, args, lang);
	}

	{ /* global args */
		obj global_args;
		if (obj_dict_geti(wk, wk->global_args, lang, &global_args)) {
			obj_array_extend(wk, args, global_args);
		}
	}

	{ /* project args */
		obj proj_args;
		if (obj_dict_geti(wk, proj->args, lang, &proj_args)) {
			obj_array_extend(wk, args, proj_args);
		}
	}

	*res = args;
	return true;
}

static enum iteration_result
setup_compiler_args_iter(struct workspace *wk, void *_ctx,
	enum compiler_language lang, obj comp_id)
{
	struct setup_compiler_args_ctx *ctx = _ctx;

	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	enum compiler_type t = comp->type;

	obj args;
	if (!get_base_compiler_args(wk, ctx->proj, ctx->tgt, lang, comp_id, &args)) {
		return ir_err;
	}

	obj inc_dirs;
	obj_array_dedup(wk, ctx->include_dirs, &inc_dirs);

	if (!obj_array_foreach(wk, inc_dirs, &(struct setup_compiler_args_includes_ctx) {
		.args = args,
		.t = t,
	}, setup_compiler_args_includes)) {
		return ir_err;
	}

	{ /* dep args */
		if (ctx->dep_args) {
			obj_array_extend(wk, args, ctx->dep_args);
		}
	}

	{ /* target args */
		obj tgt_args;
		if (obj_dict_geti(wk, ctx->tgt->args, lang, &tgt_args)
		    && tgt_args && get_obj_array(wk, tgt_args)->len) {
			obj_array_extend(wk, args, tgt_args);
		}
	}

	if (ctx->tgt->flags & build_tgt_flag_pic) {
		push_args(wk, args, compilers[t].args.pic());
	}

	if (ctx->tgt->flags & build_tgt_flag_visibility) {
		push_args(wk, args, compilers[t].args.visibility(ctx->tgt->visibility));
	}

	obj_dict_seti(wk, ctx->joined_args, lang, join_args_shell_ninja(wk, args));
	return ir_cont;
}

bool
setup_compiler_args(struct workspace *wk, const struct obj_build_target *tgt,
	const struct project *proj, obj include_dirs, obj dep_args,
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

	if (!obj_dict_foreach(wk, proj->compilers, &ctx, setup_compiler_args_iter)) {
		return false;
	}

	return true;
}

void
get_option_link_args(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, obj args_id, enum compiler_language lang)
{
#ifndef MUON_BOOTSTRAPPED
	// If we aren't bootstrapped, we don't yet have any _link_args options defined
	return;
#endif

	obj args;
	switch (lang) {
	case compiler_language_c:
		get_option_value_for_tgt(wk, proj, tgt, "c_link_args", &args);
		break;
	case compiler_language_cpp:
		get_option_value_for_tgt(wk, proj, tgt, "cpp_args", &args);
		break;
	default:
		return;
	}

	obj_array_extend(wk, args_id, args);
}

static enum iteration_result
process_rpath_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct setup_linker_args_ctx *ctx = _ctx;

	push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.rpath(get_cstr(wk, v)));

	return ir_cont;
}

static bool
setup_optional_b_args_linker(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, obj args, enum linker_type t)
{
#ifndef MUON_BOOTSTRAPPED
	// If we aren't bootstrapped, we don't yet have any b_ options defined
	return true;
#endif

	obj b_sanitize;
	get_option_value_for_tgt(wk, proj, tgt, "b_sanitize", &b_sanitize);
	if (strcmp(get_cstr(wk, b_sanitize), "none") != 0) {
		push_args(wk, args, linkers[t].args.sanitize(get_cstr(wk, b_sanitize)));
	}

	return true;
}

static enum iteration_result
push_not_found_lib_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct setup_linker_args_ctx *ctx = _ctx;

	push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.lib(get_cstr(wk, v)));
	return ir_cont;
}

void
setup_linker_args(struct workspace *wk, const struct project *proj,
	const struct obj_build_target *tgt, struct setup_linker_args_ctx *ctx)
{
	obj link_with;
	obj_array_dedup(wk, ctx->args->link_with, &link_with);
	ctx->args->link_with = link_with;

	obj link_with_not_found;
	obj_array_dedup(wk, ctx->args->link_with_not_found, &link_with_not_found);
	ctx->args->link_with_not_found = link_with_not_found;

	push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.as_needed());

	if (proj) {
		assert(tgt);

		if (!(tgt->type & tgt_shared_module)) {
			push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.no_undefined());
		}

		if (tgt->flags & build_tgt_flag_export_dynamic) {
			push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.export_dynamic());
		}

		setup_optional_b_args_linker(wk, proj, tgt, ctx->args->link_args, ctx->linker);

		{ /* option args (from option('x_link_args')) */
			get_option_link_args(wk, proj, tgt, ctx->args->link_args, ctx->link_lang);
		}

		/* global args */
		obj global_args;
		if (obj_dict_geti(wk, wk->global_link_args, ctx->link_lang, &global_args)) {
			obj_array_extend(wk, ctx->args->link_args, global_args);
		}

		/* project args */
		obj proj_args;
		if (obj_dict_geti(wk, proj->link_args, ctx->link_lang, &proj_args)) {
			obj_array_extend(wk, ctx->args->link_args, proj_args);
		}
	}

	obj_array_foreach(wk, ctx->args->rpath, ctx, process_rpath_iter);

	if (get_obj_array(wk, ctx->args->link_with)->len) {
		push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.start_group());

		obj_array_extend(wk, ctx->args->link_args, ctx->args->link_with);

		obj_array_foreach(wk, ctx->args->link_with_not_found, ctx, push_not_found_lib_iter);

		push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.end_group());
	}
}
