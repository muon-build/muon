#include "posix.h"

#include <assert.h>
#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "functions/default/options.h"
#include "functions/dependency.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static bool
get_buildtype_args(struct workspace *wk, const struct project *proj, obj args_id, enum compiler_type t)
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
	get_option(wk, proj, "buildtype", &buildtype);

	const char *str = get_cstr(wk, buildtype);

	if (strcmp(str, "custom") == 0) {
		obj optimization_id, debug_id;

		get_option(wk, proj, "optimization", &optimization_id);
		get_option(wk, proj, "debug", &debug_id);

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
			LOG_E("invalid optimization level '%s'", str);
			return false;
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

static bool
get_warning_args(struct workspace *wk, const struct project *proj, obj args_id, enum compiler_type t)
{
	obj lvl_id;
	get_option(wk, proj, "warning_level", &lvl_id);

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
		lvl = 0;
		assert(false && "invalid warning_level");
		break;
	}


	push_args(wk, args_id, compilers[t].args.warning_lvl(lvl));
	return true;
}

static bool
get_std_args(struct workspace *wk, const struct project *proj, obj args_id, enum compiler_language lang, enum compiler_type t)
{
	obj std;

	switch (lang) {
	case compiler_language_c:
		get_option(wk, proj, "c_std", &std);
		break;
	case compiler_language_cpp:
		get_option(wk, proj, "cpp_std", &std);
		break;
	default:
		return true;
	}

	const char *s = get_cstr(wk, std);

	if (strcmp(s, "none") != 0) {
		push_args(wk, args_id, compilers[t].args.set_std(s));
	}

	return true;
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

static bool
setup_optional_b_args_compiler(struct workspace *wk, const struct project *proj,
	obj args, enum compiler_type t)
{
#ifndef MUON_BOOTSTRAPPED
	// If we aren't bootstrapped, we don't yet have any b_ options defined
	return true;
#endif

	obj opt;
	get_option(wk, proj, "b_sanitize", &opt);
	if (!str_eql(get_str(wk, opt), &WKSTR("none"))) {
		push_args(wk, args, compilers[t].args.sanitize(get_cstr(wk, opt)));
	}

	obj buildtype;
	get_option(wk, proj, "buildtype", &buildtype);
	get_option(wk, proj, "b_ndebug", &opt);
	if (str_eql(get_str(wk, opt), &WKSTR("true"))
	    || (str_eql(get_str(wk, opt), &WKSTR("if-release"))
		&& str_eql(get_str(wk, buildtype), &WKSTR("release")))) {
		push_args(wk, args, compilers[t].args.define("NDEBUG"));
	}

	return true;
}

static enum iteration_result
setup_compiler_args_iter(struct workspace *wk, void *_ctx, enum compiler_language lang, obj comp_id)
{
	struct setup_compiler_args_ctx *ctx = _ctx;

	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	enum compiler_type t = comp->type;

	obj args;
	make_obj(wk, &args, obj_array);

	obj inc_dirs;
	obj_array_dedup(wk, ctx->include_dirs, &inc_dirs);

	if (!obj_array_foreach(wk, inc_dirs, &(struct setup_compiler_args_includes_ctx) {
		.args = args,
		.t = t,
	}, setup_compiler_args_includes)) {
		return ir_err;
	}

	if (!get_std_args(wk, ctx->proj, args, lang, t)) {
		LOG_E("unable to get std flag");
		return ir_err;
	} else if (!get_buildtype_args(wk, ctx->proj, args, t)) {
		LOG_E("unable to get optimization flags");
		return ir_err;
	} else if (!get_warning_args(wk, ctx->proj, args, t)) {
		LOG_E("unable to get warning flags");
		return ir_err;
	}

	if (!setup_optional_b_args_compiler(wk, ctx->proj, args, t)) {
		return false;
	}

	{ /* global args */
		obj global_args, global_args_dup;
		if (obj_dict_geti(wk, wk->global_args, lang, &global_args)) {
			obj_array_dup(wk, global_args, &global_args_dup);
			obj_array_extend(wk, args, global_args_dup);
		}
	}

	{ /* project args */
		obj proj_args, proj_args_dup;
		if (obj_dict_geti(wk, ctx->proj->args, lang, &proj_args)) {
			obj_array_dup(wk, proj_args, &proj_args_dup);
			obj_array_extend(wk, args, proj_args_dup);
		}
	}

	{ /* dep args */
		if (ctx->dep_args) {
			obj_array_extend(wk, args, ctx->dep_args);
		}
	}

	{ /* target args */
		obj tgt_args, tgt_args_dup;
		if (obj_dict_geti(wk, ctx->tgt->args, lang, &tgt_args)
		    && tgt_args && get_obj_array(wk, tgt_args)->len) {
			obj_array_dup(wk, tgt_args, &tgt_args_dup);
			obj_array_extend(wk, args, tgt_args_dup);
		}
	}

	if ((ctx->tgt->flags & build_tgt_flag_pic) ||
	    (ctx->tgt->type & (tgt_dynamic_library | tgt_shared_module))) {
		push_args(wk, args, compilers[t].args.pic());
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

static enum iteration_result
process_rpath_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct setup_linker_args_ctx *ctx = _ctx;

	push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.rpath(get_cstr(wk, v)));

	return ir_cont;
}

static bool
setup_optional_b_args_linker(struct workspace *wk, const struct project *proj,
	obj args, enum linker_type t)
{
#ifndef MUON_BOOTSTRAPPED
	// If we aren't bootstrapped, we don't yet have any b_ options defined
	return true;
#endif

	obj b_sanitize;
	get_option(wk, proj, "b_sanitize", &b_sanitize);
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

	make_obj(wk, &ctx->implicit_deps, obj_array);

	push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.as_needed());
	push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.no_undefined());

	if (proj) {
		if (tgt->flags & build_tgt_flag_export_dynamic) {
			push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.export_dynamic());
		}

		setup_optional_b_args_linker(wk, proj, ctx->args->link_args, ctx->linker);

		/* global args */
		obj global_args, global_args_dup;
		if (obj_dict_geti(wk, wk->global_link_args, ctx->link_lang, &global_args)) {
			obj_array_dup(wk, global_args, &global_args_dup);
			obj_array_extend(wk, ctx->args->link_args, global_args_dup);
		}

		/* project args */
		obj proj_args, proj_args_dup;
		if (obj_dict_geti(wk, proj->link_args, ctx->link_lang, &proj_args)) {
			obj_array_dup(wk, proj_args, &proj_args_dup);
			obj_array_extend(wk, ctx->args->link_args, proj_args_dup);
		}
	}

	obj_array_foreach(wk, ctx->args->rpath, ctx, process_rpath_iter);

	if (get_obj_array(wk, ctx->args->link_with)->len) {
		push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.start_group());

		obj dup;
		obj_array_dup(wk, ctx->args->link_with, &dup);
		obj_array_extend(wk, ctx->args->link_args, dup);

		obj_array_foreach(wk, ctx->args->link_with_not_found, ctx, push_not_found_lib_iter);

		push_args(wk, ctx->args->link_args, linkers[ctx->linker].args.end_group());
	}
}
