#include "posix.h"

#include <assert.h>
#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "functions/default/options.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static bool
get_buildtype_args(struct workspace *wk, const struct project *proj, uint32_t args_id, enum compiler_type t)
{
	uint32_t i;
	enum compiler_optimization_lvl opt;
	bool debug;

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

	uint32_t buildtype;

	if (!get_option(wk, proj, "buildtype", &buildtype)) {
		return false;
	}

	const char *str = get_cstr(wk, buildtype);

	if (strcmp(str, "custom") == 0) {
		uint32_t optimization_id, debug_id;

		if (!get_option(wk, proj, "optimization", &optimization_id)) {
			return false;
		} else if (!get_option(wk, proj, "debug", &debug_id)) {
			return false;
		}

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

		debug = get_obj(wk, debug_id)->dat.boolean;
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
get_warning_args(struct workspace *wk, const struct project *proj, uint32_t args_id, enum compiler_type t)
{
	uint32_t lvl;

	if (!get_option(wk, proj, "warning_level", &lvl)) {
		return false;
	}

	assert(get_obj(wk, lvl)->type == obj_number);

	push_args(wk, args_id, compilers[t].args.warning_lvl(get_obj(wk, lvl)->dat.num));
	return true;
}

static bool
get_std_args(struct workspace *wk, const struct project *proj, uint32_t args_id, enum compiler_type t)
{
	uint32_t std;

	if (!get_option(wk, proj, "c_std", &std)) {
		return false;
	}

	const char *s = get_cstr(wk, std);

	if (strcmp(s, "none") != 0) {
		push_args(wk, args_id, compilers[t].args.set_std(s));
	}

	return true;
}

struct setup_compiler_args_includes_ctx {
	obj args;
	enum compiler_type t;
};

static enum iteration_result
setup_compiler_args_includes(struct workspace *wk, void *_ctx, obj v)
{
	struct setup_compiler_args_includes_ctx *ctx = _ctx;
	struct obj *inc = get_obj(wk, v);
	const char *dir = get_cstr(wk, inc->dat.include_directory.path);

	if (!fs_dir_exists(dir)) {
		return ir_cont;
	}

	if (path_is_absolute(dir)) {
		char rel[PATH_MAX];
		if (!path_relative_to(rel, PATH_MAX, wk->build_root, dir)) {
			return ir_err;
		}
		dir = rel;
	}

	if (inc->dat.include_directory.is_system) {
		push_args(wk, ctx->args, compilers[ctx->t].args.include_system(dir));
	} else {
		push_args(wk, ctx->args, compilers[ctx->t].args.include(dir));
	}
	return ir_cont;
}

struct setup_compiler_args_ctx {
	const struct obj *tgt;
	const struct project *proj;

	obj include_dirs;
	obj args_dict;
};

static enum iteration_result
setup_compiler_args_iter(struct workspace *wk, void *_ctx, obj lang, obj comp_id)
{
	struct setup_compiler_args_ctx *ctx = _ctx;

	struct obj *comp = get_obj(wk, comp_id);
	assert(comp->type == obj_compiler);
	enum compiler_type t = comp->dat.compiler.type;

	uint32_t args;
	make_obj(wk, &args, obj_array);

	uint32_t proj_cwd;
	make_obj(wk, &proj_cwd, obj_string)->dat.str = ctx->proj->cwd;
	obj_array_push(wk, ctx->include_dirs, proj_cwd);

	obj inc_dirs;
	obj_array_dedup(wk, ctx->include_dirs, &inc_dirs);

	if (!obj_array_foreach(wk, inc_dirs, &(struct setup_compiler_args_includes_ctx) {
		.args = args,
		.t = t,
	}, setup_compiler_args_includes)) {
		return ir_err;
	}

	if (!get_std_args(wk, ctx->proj, args, t)) {
		LOG_E("unable to get std flag");
		return ir_err;
	} else if (!get_buildtype_args(wk, ctx->proj, args, t)) {
		LOG_E("unable to get optimization flags");
		return ir_err;
	} else if (!get_warning_args(wk, ctx->proj, args, t)) {
		LOG_E("unable to get warning flags");
		return ir_err;
	}

	{ /* project default args */
		uint32_t proj_args, proj_args_dup;
		if (obj_dict_geti(wk, ctx->proj->cfg.args, lang, &proj_args)) {
			obj_array_dup(wk, proj_args, &proj_args_dup);
			obj_array_extend(wk, args, proj_args_dup);
		}
	}

	{ /* target args */
		uint32_t tgt_args, tgt_args_dup;
		if (obj_dict_geti(wk, ctx->tgt->dat.tgt.args, lang, &tgt_args)) {
			obj_array_dup(wk, tgt_args, &tgt_args_dup);
			obj_array_extend(wk, args, tgt_args_dup);
		}
	}

	obj_dict_seti(wk, ctx->args_dict, lang, join_args_shell(wk, args));
	return ir_cont;
}

bool
setup_compiler_args(struct workspace *wk, const struct obj *tgt,
	const struct project *proj, obj include_dirs, obj args_dict)
{
	struct setup_compiler_args_ctx ctx = {
		.tgt = tgt,
		.proj = proj,
		.include_dirs = include_dirs,
		.args_dict = args_dict,
	};

	if (!obj_dict_foreach(wk, proj->compilers, &ctx, setup_compiler_args_iter)) {
		return false;
	}

	return true;
}

void
push_linker_args_link_with(struct workspace *wk, enum linker_type linker,
	obj link_args, obj link_with)
{
	push_args(wk, link_args, linkers[linker].args.as_needed());
	push_args(wk, link_args, linkers[linker].args.no_undefined());
	push_args(wk, link_args, linkers[linker].args.start_group());

	obj arr;
	obj_array_dup(wk, link_with, &arr);
	obj_array_extend(wk, link_args, arr);

	push_args(wk, link_args, linkers[linker].args.end_group());
}
