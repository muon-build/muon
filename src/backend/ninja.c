#include "posix.h"

#include <limits.h>
#include <string.h>

#include "args.h"
#include "backend/ninja.h"
#include "backend/ninja/rules.h"
#include "backend/output.h"
#include "buf_size.h"
#include "compilers.h"
#include "external/samu.h"
#include "functions/default/options.h"
#include "lang/serial.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "tests.h"

struct write_tgt_ctx {
	FILE *out;
	struct project *proj;
};

struct write_tgt_iter_ctx {
	FILE *out;
	char *tgt_parts_dir;
	struct obj *tgt;
	struct project *proj;
	uint32_t args_dict;
	uint32_t object_names;
	uint32_t order_deps;
	uint32_t include_dirs;
	uint32_t implicit_deps;
	uint32_t link_args;
	bool have_order_deps;
	bool have_implicit_deps;
	bool have_link_language;
	enum compiler_language link_language;
};

static bool
tgt_build_dir(char buf[PATH_MAX], struct workspace *wk, struct obj *tgt)
{
	if (!path_relative_to(buf, PATH_MAX, wk->build_root, get_cstr(wk, tgt->dat.tgt.build_dir))) {
		return false;
	}

	return true;
}

static bool
tgt_build_path(char buf[PATH_MAX], struct workspace *wk, struct obj *tgt)
{
	char tmp[PATH_MAX] = { 0 };
	if (!path_join(tmp, PATH_MAX, get_cstr(wk, tgt->dat.tgt.build_dir), get_cstr(wk, tgt->dat.tgt.build_name))) {
		return false;
	} else if (!path_relative_to(buf, PATH_MAX, wk->build_root, tmp)) {
		return false;
	}

	return true;
}

static void
write_escaped(FILE *f, const char *s)
{
	for (; *s; ++s) {
		if (*s == ' ' || *s == ':' || *s == '$') {
			fputc('$', f);
		}
		fputc(*s, f);
	}
}

static enum iteration_result
write_tgt_sources_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	struct obj *src = get_obj(wk, val_id);
	assert(src->type == obj_file);

	enum compiler_language fl;
	enum compiler_type ct;

	{
		uint32_t comp_id;

		// TODO put these checks into tgt creation
		if (!filename_to_compiler_language(get_cstr(wk, src->dat.file), &fl)) {
			LOG_E("unable to determine language for '%s'", get_cstr(wk, src->dat.file));
			return ir_err;

		}

		if (languages[fl].is_header) {
			return ir_cont;
		}

		if (!obj_dict_geti(wk, ctx->proj->compilers, fl, &comp_id)) {
			LOG_E("no compiler for '%s'", compiler_language_to_s(fl));
			return ir_err;
		}

		ct = get_obj(wk, comp_id)->dat.compiler.type;
	}

	/* build paths */

	char src_path[PATH_MAX];
	if (!path_relative_to(src_path, PATH_MAX, wk->build_root, get_cstr(wk, src->dat.file))) {
		return ir_err;
	}

	char rel[PATH_MAX], dest_path[PATH_MAX];
	const char *base;

	if (path_is_subpath(get_cstr(wk, ctx->tgt->dat.tgt.build_dir),
		get_cstr(wk, src->dat.file))) {
		base = get_cstr(wk, ctx->tgt->dat.tgt.build_dir);
	} else if (path_is_subpath(get_cstr(wk, ctx->tgt->dat.tgt.cwd),
		get_cstr(wk, src->dat.file))) {
		base = get_cstr(wk, ctx->tgt->dat.tgt.cwd);
	} else {
		base = wk->source_root;
	}

	if (!path_relative_to(rel, PATH_MAX, base, get_cstr(wk, src->dat.file))) {
		return ir_err;
	} else if (!path_join(dest_path, PATH_MAX, ctx->tgt_parts_dir, rel)) {
		return ir_err;
	} else if (!path_add_suffix(dest_path, PATH_MAX, ".o")) {
		return ir_err;
	}

	obj_array_push(wk, ctx->object_names, make_str(wk, dest_path));

	/* build rules and args */

	uint32_t args_id;
	if (!obj_dict_geti(wk, ctx->args_dict, fl, &args_id)) {
		LOG_E("couldn't get args for language %s", compiler_language_to_s(fl));
		return ir_err;
	}

	fputs("build ", ctx->out);
	write_escaped(ctx->out, dest_path);
	fprintf(ctx->out, ": %s_COMPILER ", compiler_language_to_s(fl));
	write_escaped(ctx->out, src_path);
	if (ctx->have_order_deps) {
		fprintf(ctx->out, " || %s", get_cstr(wk, ctx->order_deps));
	}
	fputc('\n', ctx->out);

	fprintf(ctx->out,
		" ARGS = %s\n", get_cstr(wk, args_id));

	if (compilers[ct].deps) {
		fprintf(ctx->out,
			" DEPFILE = %s.d\n"
			" DEPFILE_UNQUOTED = %s.d\n",
			dest_path, dest_path);
	}

	fputc('\n', ctx->out);

	return ir_cont;
}

static enum iteration_result
process_source_includes_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	struct obj *src = get_obj(wk, val_id);
	assert(src->type == obj_file);

	enum compiler_language fl;

	if (!filename_to_compiler_language(get_cstr(wk, src->dat.file), &fl)) {
		LOG_E("unable to determine language for '%s'", get_cstr(wk, src->dat.file));
		return ir_err;

	}

	if (!languages[fl].is_header) {
		return ir_cont;
	}

	char dir[PATH_MAX], path[PATH_MAX];

	if (!path_relative_to(path, PATH_MAX, wk->build_root, get_cstr(wk, src->dat.file))) {
		return ir_err;
	}

	obj_array_push(wk, ctx->order_deps, make_str(wk, path));
	ctx->have_order_deps = true;

	if (!path_dirname(dir, PATH_MAX, path)) {
		return ir_err;
	}

	obj_array_push(wk, ctx->include_dirs, make_str(wk, dir));

	return ir_cont;
}

static enum iteration_result
process_dep_args_includes_iter(struct workspace *wk, void *_ctx, uint32_t inc_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;

	assert(get_obj(wk, inc_id)->type == obj_file);

	char path[PATH_MAX];
	if (!path_relative_to(path, PATH_MAX, wk->build_root, get_cstr(wk, get_obj(wk, inc_id)->dat.file))) {
		return ir_err;
	}

	obj_array_push(wk, ctx->include_dirs, make_str(wk, path));
	return ir_cont;
}

static enum iteration_result
process_dep_args_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;

	struct obj *o = get_obj(wk, val_id);

	switch (o->type) {
	case obj_external_library: {
		if (ctx->tgt->dat.tgt.type == tgt_executable) {
			obj str;
			make_obj(wk, &str, obj_string)->dat.str = o->dat.external_library.full_path;

			obj_array_push(wk, ctx->link_args, str);
		}
		break;
	}
	case obj_dependency: {
		if (o->dat.dep.include_directories) {
			struct obj *inc = get_obj(wk, o->dat.dep.include_directories);
			assert(inc->type == obj_array);
			if (!obj_array_foreach_flat(wk, o->dat.dep.include_directories,
				_ctx, process_dep_args_includes_iter)) {
				return ir_err;
			}
		}

		if (o->dat.dep.link_args) {
			obj dup;
			obj_array_dup(wk, o->dat.dep.link_args, &dup);
			obj_array_extend(wk, ctx->link_args, dup);
		}
		break;
	}
	default:
		LOG_E("invalid type for dependency: %s", obj_type_to_s(o->type));
		return ir_err;
	}

	return ir_cont;
}

static enum iteration_result process_dep_links_iter(struct workspace *wk, void *_ctx, uint32_t val_id);

static enum iteration_result
process_link_with_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;

	struct obj *tgt = get_obj(wk, val_id);

	switch (tgt->type) {
	case  obj_build_target: {
		char path[PATH_MAX];

		if (!tgt_build_path(path, wk, tgt)) {
			return ir_err;
		}

		if (ctx->tgt->dat.tgt.type == tgt_executable) {
			uint32_t str = make_str(wk, path);
			obj_array_push(wk, ctx->implicit_deps, str);
			ctx->have_implicit_deps = true;
			obj_array_push(wk, ctx->link_args, str);
		}

		if (!tgt_build_dir(path, wk, tgt)) {
			return ir_err;
		}

		obj_array_push(wk, ctx->include_dirs, make_str(wk, path));

		if (tgt->dat.tgt.deps) {
			if (!obj_array_foreach(wk, tgt->dat.tgt.deps, ctx, process_dep_links_iter)) {
				return ir_err;
			}
		}

		if (tgt->dat.tgt.include_directories) {
			if (!obj_array_foreach_flat(wk, tgt->dat.tgt.include_directories, _ctx, process_dep_args_includes_iter)) {
				return ir_err;
			}
		}
		break;
	}
	case obj_string:
		if (ctx->tgt->dat.tgt.type == tgt_executable) {
			obj_array_push(wk, ctx->link_args, val_id);
		}
		break;
	default:
		LOG_E("invalid type for link_with: '%s'", obj_type_to_s(tgt->type));
		return ir_err;
	}

	return ir_cont;
}

static enum iteration_result
process_dep_links_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	struct obj *dep = get_obj(wk, val_id);

	switch (dep->type) {
	case obj_dependency:
		if (dep->dat.dep.link_with) {
			if (!obj_array_foreach(wk, dep->dat.dep.link_with, _ctx, process_link_with_iter)) {
				return ir_err;
			}
		}
		break;
	case obj_external_library:
		if (ctx->tgt->dat.tgt.type == tgt_executable) {
			uint32_t val_id;
			make_obj(wk, &val_id, obj_string)->dat.str =
				dep->dat.external_library.full_path;
			obj_array_push(wk, ctx->link_args, val_id);
		}
		break;
	default:
		LOG_E("invalid type for dependency: %s", obj_type_to_s(dep->type));
		return ir_err;
	}

	return ir_cont;
}

static enum iteration_result
process_include_dirs_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	uint32_t *include_dirs = _ctx;
	struct obj *inc = get_obj(wk, val_id);
	assert(inc->type == obj_file);

	uint32_t str;
	make_obj(wk, &str, obj_string)->dat.str = inc->dat.file;

	obj_array_push(wk, *include_dirs, str);
	return ir_cont;
}

static bool
get_buildtype_args(struct workspace *wk, struct project *proj, uint32_t args_id, enum compiler_type t)
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
get_warning_args(struct workspace *wk, struct project *proj, uint32_t args_id, enum compiler_type t)
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
get_std_args(struct workspace *wk, struct project *proj, uint32_t args_id, enum compiler_type t)
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
	uint32_t args;
	enum compiler_type t;
};

static enum iteration_result
setup_compiler_args_includes(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	const char *dir = get_cstr(wk, v_id);

	if (path_is_absolute(dir)) {
		char rel[PATH_MAX];
		if (!path_relative_to(rel, PATH_MAX, wk->build_root, dir)) {
			return ir_err;
		}
		dir = rel;
	}

	struct setup_compiler_args_includes_ctx *ctx = _ctx;
	push_args(wk, ctx->args, compilers[ctx->t].args.include(dir));
	return ir_cont;
}

static enum iteration_result
setup_compiler_args_iter(struct workspace *wk, void *_ctx, uint32_t l, uint32_t comp_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;

	struct obj *comp = get_obj(wk, comp_id);
	assert(comp->type == obj_compiler);
	enum compiler_type t = comp->dat.compiler.type;

	uint32_t args;
	make_obj(wk, &args, obj_array);

	uint32_t proj_cwd;
	make_obj(wk, &proj_cwd, obj_string)->dat.str = ctx->proj->cwd;
	obj_array_push(wk, ctx->include_dirs, proj_cwd);
	if (!obj_array_foreach(wk, ctx->include_dirs, &(struct setup_compiler_args_includes_ctx) {
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
		if (obj_dict_geti(wk, ctx->proj->cfg.args, l, &proj_args)) {
			obj_array_dup(wk, proj_args, &proj_args_dup);
			obj_array_extend(wk, args, proj_args_dup);
		}
	}

	{ /* target args */
		uint32_t tgt_args, tgt_args_dup;
		if (obj_dict_geti(wk, ctx->tgt->dat.tgt.args, l, &tgt_args)) {
			obj_array_dup(wk, tgt_args, &tgt_args_dup);
			obj_array_extend(wk, args, tgt_args_dup);
		}
	}

	obj_dict_seti(wk, ctx->args_dict, l, join_args_shell(wk, args));
	return ir_cont;
}

static enum iteration_result
determine_linker_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	struct obj *src = get_obj(wk, v_id);
	assert(src->type == obj_file);

	enum compiler_language fl;

	if (!filename_to_compiler_language(get_cstr(wk, src->dat.file), &fl)) {
		LOG_E("unable to determine language for '%s'", get_cstr(wk, src->dat.file));
		return ir_err;
	}

	switch (fl) {
	case compiler_language_c_hdr:
	case compiler_language_cpp_hdr:
		return ir_cont;
	case compiler_language_c:
		if (!ctx->have_link_language) {
			ctx->link_language = compiler_language_c;
		}
		break;
	case compiler_language_cpp:
		if (!ctx->have_link_language
		    || ctx->link_language == compiler_language_c) {
			ctx->link_language = compiler_language_cpp;
		}
		break;
	case compiler_language_count:
		assert(false);
		return ir_err;
	}

	ctx->have_link_language = true;
	return ir_cont;
}

static enum iteration_result
write_build_tgt(struct workspace *wk, void *_ctx, uint32_t tgt_id)
{
	FILE *out = ((struct write_tgt_ctx *)_ctx)->out;
	struct project *proj = ((struct write_tgt_ctx *)_ctx)->proj;

	struct obj *tgt = get_obj(wk, tgt_id);
	LOG_I("writing rules for target '%s'", get_cstr(wk, tgt->dat.tgt.build_name));

	char path[PATH_MAX];
	if (!tgt_build_path(path, wk, tgt)) {
		return ir_err;
	} else if (!path_add_suffix(path, PATH_MAX, ".p")) {
		return ir_err;
	}

	struct write_tgt_iter_ctx ctx = {
		.tgt = tgt,
		.proj = proj,
		.out = out,
		.tgt_parts_dir = path,
	};

	make_obj(wk, &ctx.object_names, obj_array);
	make_obj(wk, &ctx.order_deps, obj_array);
	if (tgt->dat.tgt.link_args) {
		obj_array_dup(wk, tgt->dat.tgt.link_args, &ctx.link_args);
	} else {
		make_obj(wk, &ctx.link_args, obj_array);
	}
	make_obj(wk, &ctx.implicit_deps, obj_array);
	make_obj(wk, &ctx.include_dirs, obj_array);

	obj_array_push(wk, ctx.include_dirs, make_str(wk, wk->build_root));

	enum linker_type linker;

	{ /* determine linker */
		if (!obj_array_foreach(wk, tgt->dat.tgt.src, &ctx, determine_linker_iter)) {
			return ir_err;
		} else if (!ctx.have_link_language) {
			LOG_E("unable to determine linker for target");
			return ir_err;
		}

		uint32_t comp_id;
		if (!obj_dict_geti(wk, ctx.proj->compilers, ctx.link_language, &comp_id)) {
			LOG_E("no compiler defined for language %s", compiler_language_to_s(ctx.link_language));
			return false;
		}

		linker = compilers[get_obj(wk, comp_id)->dat.compiler.type].linker;
	}

	const char *linker_type;
	switch (tgt->dat.tgt.type) {
	case tgt_executable:
		linker_type = compiler_language_to_s(ctx.link_language);
		push_args(wk, ctx.link_args, linkers[linker].args.as_needed());
		push_args(wk, ctx.link_args, linkers[linker].args.no_undefined());
		push_args(wk, ctx.link_args, linkers[linker].args.start_group());
		break;
	case tgt_library:
		linker_type = "STATIC";
		obj_array_push(wk, ctx.link_args, make_str(wk, "csrD"));
		break;
	default:
		assert(false);
		return ir_err;
	}

	{ /* includes */
		if (tgt->dat.tgt.include_directories) {
			struct obj *inc = get_obj(wk, tgt->dat.tgt.include_directories);
			assert(inc->type == obj_array);
			if (!obj_array_foreach_flat(wk, tgt->dat.tgt.include_directories, &ctx.include_dirs, process_include_dirs_iter)) {
				return ir_err;
			}
		}

		{ /* dep includes */
			if (tgt->dat.tgt.deps) {
				if (!obj_array_foreach(wk, tgt->dat.tgt.deps, &ctx, process_dep_args_iter)) {
					return ir_err;
				}
			}
		}

		/* sources includes */
		if (!obj_array_foreach(wk, tgt->dat.tgt.src, &ctx, process_source_includes_iter)) {
			return ir_err;
		}
	}

	{ /* dependencies / link_with */
		if (tgt->dat.tgt.deps) {
			if (!obj_array_foreach(wk, tgt->dat.tgt.deps, &ctx, process_dep_links_iter)) {
				return ir_err;
			}
		}

		if (tgt->dat.tgt.link_with) {
			if (!obj_array_foreach(wk, tgt->dat.tgt.link_with, &ctx, process_link_with_iter)) {
				return ir_err;
			}
		}
	}

	make_obj(wk, &ctx.args_dict, obj_dict);
	if (!obj_dict_foreach(wk, proj->compilers, &ctx, setup_compiler_args_iter)) {
		return ir_err;
	}

	ctx.order_deps = join_args_ninja(wk, ctx.order_deps);

	{ /* sources */
		if (!obj_array_foreach(wk, tgt->dat.tgt.src, &ctx, write_tgt_sources_iter)) {
			return ir_err;
		}
	}

	if (tgt->dat.tgt.type == tgt_executable) {
		push_args(wk, ctx.link_args, linkers[linker].args.end_group());
	}

	if (!tgt_build_path(path, wk, tgt)) {
		return ir_err;
	}

	ctx.implicit_deps = join_args_ninja(wk, ctx.implicit_deps);

	fputs("build ", out);
	write_escaped(out, path);
	fprintf(out, ": %s_LINKER ", linker_type);
	fputs(get_cstr(wk, join_args_ninja(wk, ctx.object_names)), out);
	if (ctx.have_implicit_deps) {
		fputs(" | ", out);
		fputs(get_cstr(wk, ctx.implicit_deps), out);
	}
	if (ctx.have_order_deps) {
		fputs(" || ", out);
		fputs(get_cstr(wk, ctx.order_deps), out);
	}
	fprintf(out, "\n LINK_ARGS = %s\n\n", get_cstr(wk, join_args_shell(wk, ctx.link_args)));

	return ir_cont;
}

static enum iteration_result
relativize_paths_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	uint32_t *dest = _ctx;

	struct obj *file = get_obj(wk, val_id);
	assert(file->type == obj_file);

	char buf[PATH_MAX];

	if (!path_relative_to(buf, PATH_MAX, wk->build_root, get_cstr(wk, file->dat.file))) {
		return ir_err;
	}

	obj_array_push(wk, *dest, make_str(wk, buf));
	return ir_cont;
}

static enum iteration_result
write_custom_tgt(struct workspace *wk, void *_ctx, uint32_t tgt_id)
{
	FILE *out = ((struct write_tgt_ctx *)_ctx)->out;

	struct obj *tgt = get_obj(wk, tgt_id);
	LOG_I("writing rules for custom target '%s'", get_cstr(wk, tgt->dat.custom_target.name));

	uint32_t outputs, inputs, cmdline;

	make_obj(wk, &inputs, obj_array);
	if (!obj_array_foreach(wk, tgt->dat.custom_target.input, &inputs, relativize_paths_iter)) {
		return ir_err;
	}

	make_obj(wk, &outputs, obj_array);
	if (!obj_array_foreach(wk, tgt->dat.custom_target.output, &outputs, relativize_paths_iter)) {
		return ir_err;
	}

	make_obj(wk, &cmdline, obj_array);
	if (tgt->dat.custom_target.flags & custom_target_capture) {
		obj_array_push(wk, cmdline, make_str(wk, wk->argv0));
		obj_array_push(wk, cmdline, make_str(wk, "internal"));
		obj_array_push(wk, cmdline, make_str(wk, "exe"));
		obj_array_push(wk, cmdline, make_str(wk, "-c"));

		uint32_t elem;
		if (!obj_array_index(wk, tgt->dat.custom_target.output, 0, &elem)) {
			assert(false && "custom target with no output");
			return ir_err;
		}

		if (relativize_paths_iter(wk, &cmdline, elem) == ir_err) {
			return ir_err;
		}

		obj_array_push(wk, cmdline, make_str(wk, "--"));
	}

	uint32_t tgt_args;
	if (!arr_to_args(wk, tgt->dat.custom_target.args, &tgt_args)) {
		return ir_err;
	}

	uint32_t cmd;
	obj_array_index(wk, tgt->dat.custom_target.args, 0, &cmd);
	char cmd_escaped[PATH_MAX];
	if (!ninja_escape(cmd_escaped, PATH_MAX, get_cstr(wk, cmd))) {
		return ir_err;
	}

	obj_array_extend(wk, cmdline, tgt_args);

	outputs = join_args_ninja(wk, outputs);
	inputs = join_args_ninja(wk, inputs);
	cmdline = join_args_shell(wk, cmdline);

	fprintf(out, "build %s: CUSTOM_COMMAND %s | %s\n"
		" COMMAND = %s\n"
		" DESCRIPTION = %s\n\n",
		get_cstr(wk, outputs),
		get_cstr(wk, inputs),
		cmd_escaped,
		get_cstr(wk, cmdline),
		get_cstr(wk, cmdline)
		);

	return ir_cont;
}

static enum iteration_result
write_tgt_iter(struct workspace *wk, void *_ctx, uint32_t tgt_id)
{
	switch (get_obj(wk, tgt_id)->type) {
	case obj_build_target:
		return write_build_tgt(wk, _ctx, tgt_id);
	case obj_custom_target:
		return write_custom_tgt(wk, _ctx, tgt_id);
	default:
		LOG_E("invalid tgt type '%s'", obj_type_to_s(get_obj(wk, tgt_id)->type));
		return ir_err;
	}
}

static bool
ninja_write_build(struct workspace *wk, void *_ctx, FILE *out)
{
	if (!ninja_write_rules(out, wk, darr_get(&wk->projects, 0))) {
		return false;
	}

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = darr_get(&wk->projects, i);

		struct write_tgt_ctx ctx = { .out = out, .proj = proj };

		if (!obj_array_foreach(wk, proj->targets, &ctx, write_tgt_iter)) {
			return false;
		}
	}

	return true;
}

static bool
ninja_write_tests(struct workspace *wk, void *_ctx, FILE *out)
{
	LOG_I("writing tests");

	obj tests;
	make_obj(wk, &tests, obj_dict);

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = darr_get(&wk->projects, i);

		if (proj->tests) {
			uint32_t res, key;
			make_obj(wk, &key, obj_string)->dat.str = proj->cfg.name;

			if (obj_dict_index(wk, tests, key, &res)) {
				LOG_E("project defined multiple times");
				return false;
			}

			obj_dict_set(wk, tests, key, proj->tests);
		}
	}

	return serial_dump(wk, tests, out);
}

static bool
ninja_write_install(struct workspace *wk, void *_ctx, FILE *out)
{
	return serial_dump(wk, wk->install, out);
}

static bool
ninja_write_setup(struct workspace *wk, void *_ctx, FILE *f)
{
	struct project *proj;
	uint32_t i;
	char buf[2048];
	uint32_t opts;
	proj = darr_get(&wk->projects, 0);

	if (!obj_dict_dup(wk, proj->opts, &opts)) {
		return false;
	}

	for (i = 1; i < wk->projects.len; ++i) {
		proj = darr_get(&wk->projects, i);
		uint32_t str;
		make_obj(wk, &str, obj_string)->dat.str = proj->subproject_name;
		obj_dict_set(wk, opts, str, proj->opts);
	}

	if (!obj_to_s(wk, opts, buf, 2048)) {
		return false;
	}

	fprintf(f, "setup(\n\t'%s',\n\tsource: '%s',\n\toptions: %s\n)\n", wk->build_root, wk->source_root, buf);
	return true;
}

bool
ninja_write_all(struct workspace *wk)
{
	if (!(with_open(wk->build_root, "build.ninja", wk, NULL, ninja_write_build)
	      && with_open(wk->muon_private, output_path.tests, wk, NULL, ninja_write_tests)
	      && with_open(wk->muon_private, output_path.install, wk, NULL, ninja_write_install)
	      && with_open(wk->muon_private, output_path.setup, wk, NULL, ninja_write_setup)
	      )) {
		return false;
	}

	/* compile_commands.json */
	if (have_samu) {
		char compile_commands[PATH_MAX];
		if (!path_join(compile_commands, PATH_MAX, wk->build_root, "compile_commands.json")) {
			return false;
		}

		if (!muon_samu_compdb(wk->build_root, compile_commands)) {
			return false;
		}
	}

	return true;
}
