#include "posix.h"

#include <limits.h>
#include <string.h>

#include "buf_size.h"
#include "compilers.h"
#include "external/samu.h"
#include "functions/default/options.h"
#include "lang/workspace.h"
#include "log.h"
#include "output/output.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "tests.h"

const struct outpath outpath = {
	.private_dir = "muon-private",
	.setup = "setup.meson",
	.tests = "tests",
};

struct concat_strings_ctx {
	uint32_t *res;
};

struct output {
	FILE *build_ninja,
	     *tests,
	     *opts;
	bool compile_commands_comma;
};

static bool
concat_str(struct workspace *wk, uint32_t *dest, const char *s)
{
	if (strlen(s) >= BUF_SIZE_2k) {
		LOG_E("string too long in concat strings: '%s'", s);
		return false;
	}

	static char buf[BUF_SIZE_2k + 2] = { 0 };
	uint32_t i = 0;
	bool quote = false;

	for (; *s; ++s) {
		if (*s == ' ') {
			quote = true;
			buf[i] = '$';
			++i;
		} else if (*s == '"') {
			quote = true;
		}

		buf[i] = *s;
		++i;
	}

	buf[i] = 0;
	++i;

	if (quote) {
		wk_str_app(wk, dest, "'");
	}

	wk_str_app(wk, dest, buf);

	if (quote) {
		wk_str_app(wk, dest, "'");
	}

	wk_str_app(wk, dest, " ");
	return true;
}

static bool
tgt_build_dir(char buf[PATH_MAX], struct workspace *wk, struct obj *tgt)
{
	if (!path_relative_to(buf, PATH_MAX, wk->build_root, wk_str(wk, tgt->dat.tgt.build_dir))) {
		return false;
	}

	return true;
}

static bool
tgt_build_path(char buf[PATH_MAX], struct workspace *wk, struct obj *tgt)
{
	char tmp[PATH_MAX] = { 0 };
	if (!path_join(tmp, PATH_MAX, wk_str(wk, tgt->dat.tgt.build_dir), wk_str(wk, tgt->dat.tgt.build_name))) {
		return false;
	} else if (!path_relative_to(buf, PATH_MAX, wk->build_root, tmp)) {
		return false;
	}

	return true;
}

static bool
strobj(struct workspace *wk, uint32_t *dest, uint32_t src)
{
	struct obj *obj = get_obj(wk, src);

	switch (obj->type) {
	case obj_string:
		*dest = obj->dat.str;
		return true;
	case obj_file:
		*dest = obj->dat.file;
		return true;

	case obj_build_target: {
		char tmp1[PATH_MAX], path[PATH_MAX];
		if (!tgt_build_path(tmp1, wk, obj)) {
			return false;
		} else if (!path_executable(path, PATH_MAX, tmp1)) {
			return false;
		}

		*dest = wk_str_push(wk, path);
		return true;
	}
	default:
		LOG_E("cannot convert '%s' to string", obj_type_to_s(obj->type));
		return false;
	}
}

static bool
concat_strobj(struct workspace *wk, uint32_t *dest, uint32_t src)
{
	uint32_t str;
	if (!strobj(wk, &str, src)) {
		return false;
	}

	return concat_str(wk, dest, wk_str(wk, str));
}

static enum iteration_result
concat_strings_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct concat_strings_ctx *ctx = _ctx;
	if (!concat_strobj(wk, ctx->res, val)) {
		return ir_err;
	}

	return ir_cont;
}

static bool
concat_strings(struct workspace *wk, uint32_t arr, uint32_t *res)
{
	if (!*res) {
		*res = wk_str_push(wk, "");
	}

	struct concat_strings_ctx ctx = {
		.res = res,
	};

	return obj_array_foreach(wk, arr, &ctx, concat_strings_iter);
}

static const char *
get_dict_str(struct workspace *wk, uint32_t dict, const char *k, const char *fallback)
{
	uint32_t res;

	if (!obj_dict_index_strn(wk, dict, k, strlen(k), &res)) {
		return fallback;
	} else {
		return wk_objstr(wk, res);
	}
}

static void
push_args(struct workspace *wk, uint32_t arr, const struct compiler_args *args)
{
	uint32_t i;
	for (i = 0; i < args->len; ++i) {
		obj_array_push(wk, arr, make_str(wk, args->args[i]));
	}
}

struct join_args_iter_ctx {
	uint32_t i, len;
	uint32_t *obj;
};

static enum iteration_result
join_args_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct join_args_iter_ctx *ctx = _ctx;

	assert(get_obj(wk, val)->type == obj_string);

	const char *s = wk_objstr(wk, val);
	bool needs_escaping = false;

	for (; *s; ++s) {
		if (*s == '"' || *s == ' ') {
			needs_escaping = true;
		}
	}

	if (needs_escaping) {
		wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, "'");
	}

	wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, wk_objstr(wk, val));

	if (needs_escaping) {
		wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, "'");
	}

	if (ctx->i < ctx->len - 1) {
		wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, " ");
	}

	++ctx->i;

	return ir_cont;
}

static uint32_t
join_args(struct workspace *wk, uint32_t arr)
{
	uint32_t obj;
	make_obj(wk, &obj, obj_string)->dat.str = wk_str_push(wk, "");

	struct join_args_iter_ctx ctx = {
		.obj = &obj,
		.len = get_obj(wk, arr)->dat.arr.len
	};

	if (!obj_array_foreach(wk, arr, &ctx, join_args_iter)) {
		assert(false);
		return 0;
	}

	return obj;
}

static enum iteration_result
write_compiler_rule_iter(struct workspace *wk, void *_ctx, uint32_t l, uint32_t comp_id)
{
	FILE *out = _ctx;
	struct obj *comp = get_obj(wk, comp_id);
	assert(comp->type == obj_compiler);

	enum compiler_type t = comp->dat.compiler.type;

	const char *deps = NULL;
	switch (compilers[t].deps) {
	case compiler_deps_none:
		break;
	case compiler_deps_gcc:
		deps = "gcc";
		break;
	case compiler_deps_msvc:
		deps = "msvc";
		break;
	}

	uint32_t args, compiler_name;
	make_obj(wk, &args, obj_array);
	make_obj(wk, &compiler_name, obj_string)->dat.str = comp->dat.compiler.name;
	obj_array_push(wk, args, compiler_name);
	obj_array_push(wk, args, make_str(wk, "$ARGS"));
	if (compilers[t].deps) {
		push_args(wk, args, compilers[t].args.deps("$out", "$DEPFILE"));
	}
	push_args(wk, args, compilers[t].args.output("$out"));
	push_args(wk, args, compilers[t].args.compile_only());
	obj_array_push(wk, args, make_str(wk, "$in"));
	uint32_t command = join_args(wk, args);

	fprintf(out, "rule %s_COMPILER\n"
		" command = %s\n",
		compiler_language_to_s(l),
		wk_objstr(wk, command));
	if (compilers[t].deps) {
		fprintf(out,
			" deps = %s\n"
			" depfile = $DEPFILE_UNQUOTED\n",
			deps);
	}
	fprintf(out,
		" description = compiling %s $out\n\n",
		compiler_language_to_s(l));

	fprintf(out, "rule %s_LINKER\n"
		" command = %s $ARGS -o $out $in $LINK_ARGS\n"
		" description = Linking target $out\n\n",
		compiler_language_to_s(l),
		wk_objstr(wk, compiler_name));

	return ir_cont;
}

static void
write_hdr(FILE *out, struct workspace *wk, struct project *main_proj)
{
	uint32_t sep, sources;
	make_obj(wk, &sep, obj_string)->dat.str = wk_str_push(wk, " ");
	obj_array_join(wk, wk->sources, sep, &sources);

	fprintf(
		out,
		"# This is the build file for project \"%s\"\n"
		"# It is autogenerated by the muon build system.\n"
		"ninja_required_version = 1.7.1\n\n",
		wk_str(wk, main_proj->cfg.name)
		);

	// TODO: setup compiler rules for subprojects
	obj_dict_foreach(wk, main_proj->compilers, out, write_compiler_rule_iter);

	fprintf(out,
		"rule STATIC_LINKER\n"
		" command = rm -f $out && %s $LINK_ARGS $out $in\n"
		" description = Linking static target $out\n"
		"\n"
		"rule CUSTOM_COMMAND\n"
		" command = $COMMAND\n"
		" description = $DESCRIPTION\n"
		" restat = 1\n"
		"\n"
		"rule REGENERATE_BUILD\n"
		" command = %s build -r -c %s%c%s\n"
		" description = Regenerating build files.\n"
		" generator = 1\n"
		"\n"
		"build build.ninja: REGENERATE_BUILD %s\n"
		" pool = console\n"
		"\n"
		"# targets\n\n",
		get_dict_str(wk, wk->binaries, "ar", "ar"),
		wk->argv0,
		outpath.private_dir, PATH_SEP, outpath.setup,
		wk_objstr(wk, sources)
		);
}

struct write_tgt_iter_ctx {
	char *tgt_parts_dir;
	struct obj *tgt;
	struct project *proj;
	struct output *output;
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
		if (!filename_to_compiler_language(wk_str(wk, src->dat.file), &fl)) {
			LOG_E("unable to determine language for '%s'", wk_str(wk, src->dat.file));
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
	if (!path_relative_to(src_path, PATH_MAX, wk->build_root, wk_str(wk, src->dat.file))) {
		return ir_err;
	}

	char rel[PATH_MAX], dest_path[PATH_MAX];
	const char *base;

	if (path_is_subpath(wk_str(wk, ctx->tgt->dat.tgt.build_dir),
		wk_str(wk, src->dat.file))) {
		base = wk_str(wk, ctx->tgt->dat.tgt.build_dir);
	} else if (path_is_subpath(wk_str(wk, ctx->tgt->dat.tgt.cwd),
		wk_str(wk, src->dat.file))) {
		base = wk_str(wk, ctx->tgt->dat.tgt.cwd);
	} else {
		base = wk->source_root;
	}

	if (!path_relative_to(rel, PATH_MAX, base, wk_str(wk, src->dat.file))) {
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

	fputs("build ", ctx->output->build_ninja);
	write_escaped(ctx->output->build_ninja, dest_path);
	fprintf(ctx->output->build_ninja, ": %s_COMPILER ", compiler_language_to_s(fl));
	write_escaped(ctx->output->build_ninja, src_path);
	if (ctx->have_order_deps) {
		fprintf(ctx->output->build_ninja, " || %s", wk_objstr(wk, ctx->order_deps));
	}
	fputc('\n', ctx->output->build_ninja);

	fprintf(ctx->output->build_ninja,
		" ARGS = %s\n", wk_objstr(wk, args_id));

	if (compilers[ct].deps) {
		fprintf(ctx->output->build_ninja,
			" DEPFILE = %s.d\n"
			" DEPFILE_UNQUOTED = %s.d\n",
			dest_path, dest_path);
	}

	fputc('\n', ctx->output->build_ninja);

	return ir_cont;
}

static enum iteration_result
process_source_includes_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	struct obj *src = get_obj(wk, val_id);
	assert(src->type == obj_file);

	enum compiler_language fl;

	if (!filename_to_compiler_language(wk_str(wk, src->dat.file), &fl)) {
		LOG_E("unable to determine language for '%s'", wk_str(wk, src->dat.file));
		return ir_err;

	}

	if (!languages[fl].is_header) {
		return ir_cont;
	}

	char dir[PATH_MAX], path[PATH_MAX];

	if (!path_relative_to(path, PATH_MAX, wk->build_root, wk_str(wk, src->dat.file))) {
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
	uint32_t str;
	make_obj(wk, &str, obj_string)->dat.str = get_obj(wk, inc_id)->dat.file;
	obj_array_push(wk, ctx->include_dirs, str);
	return ir_cont;
}

static enum iteration_result
process_dep_args_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct obj *dep = get_obj(wk, val_id);

	if (dep->dat.dep.include_directories) {
		struct obj *inc = get_obj(wk, dep->dat.dep.include_directories);
		assert(inc->type == obj_array);
		if (!obj_array_foreach_flat(wk, dep->dat.dep.include_directories,
			_ctx, process_dep_args_includes_iter)) {
			return ir_err;
		}
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

struct write_tgt_ctx {
	struct output *output;
	struct project *proj;
};

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

	const char *str = wk_objstr(wk, buildtype);

	if (strcmp(str, "custom") == 0) {
		uint32_t optimization_id, debug_id;

		if (!get_option(wk, proj, "optimization", &optimization_id)) {
			return false;
		} else if (!get_option(wk, proj, "debug", &debug_id)) {
			return false;
		}

		str = wk_objstr(wk, optimization_id);
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

	const char *s = wk_objstr(wk, std);

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
	char *dir = wk_objstr(wk, v_id);

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

	obj_dict_set(wk, ctx->args_dict, l, join_args(wk, args));
	return ir_cont;
}

static enum iteration_result
determine_linker_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	struct obj *src = get_obj(wk, v_id);
	assert(src->type == obj_file);

	enum compiler_language fl;

	if (!filename_to_compiler_language(wk_str(wk, src->dat.file), &fl)) {
		LOG_E("unable to determine language for '%s'", wk_str(wk, src->dat.file));
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
	struct output *output = ((struct write_tgt_ctx *)_ctx)->output;
	struct project *proj = ((struct write_tgt_ctx *)_ctx)->proj;

	struct obj *tgt = get_obj(wk, tgt_id);
	LOG_I("writing rules for target '%s'", wk_str(wk, tgt->dat.tgt.build_name));

	char path[PATH_MAX];
	if (!tgt_build_path(path, wk, tgt)) {
		return ir_err;
	} else if (!path_add_suffix(path, PATH_MAX, ".p")) {
		return ir_err;
	}

	struct write_tgt_iter_ctx ctx = {
		.tgt = tgt,
		.proj = proj,
		.output = output,
		.tgt_parts_dir = path,
	};

	make_obj(wk, &ctx.object_names, obj_array);
	make_obj(wk, &ctx.order_deps, obj_array);
	make_obj(wk, &ctx.link_args, obj_array);
	make_obj(wk, &ctx.implicit_deps, obj_array);
	make_obj(wk, &ctx.include_dirs, obj_array);

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
			assert(false);
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

	/* how to determine which linker to use? */

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

	ctx.order_deps = join_args(wk, ctx.order_deps);

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

	ctx.implicit_deps = join_args(wk, ctx.implicit_deps);

	fputs("build ", output->build_ninja);
	write_escaped(output->build_ninja, path);
	fprintf(output->build_ninja, ": %s_LINKER ", linker_type);
	fputs(wk_objstr(wk, join_args(wk, ctx.object_names)), output->build_ninja); // escape
	if (ctx.have_implicit_deps) {
		fputs(" | ", output->build_ninja);
		fputs(wk_objstr(wk, ctx.implicit_deps), output->build_ninja); // escape
	}
	if (ctx.have_order_deps) {
		fputs(" || ", output->build_ninja);
		fputs(wk_objstr(wk, ctx.order_deps), output->build_ninja); // escape
	}
	fprintf(output->build_ninja, "\n LINK_ARGS = %s\n\n", wk_objstr(wk, join_args(wk, ctx.link_args)));

	return ir_cont;
}

static enum iteration_result
custom_tgt_outputs_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	uint32_t *dest = _ctx;

	struct obj *out = get_obj(wk, val_id);
	assert(out->type == obj_file);

	char buf[PATH_MAX];

	if (!path_relative_to(buf, PATH_MAX, wk->build_root, wk_str(wk, out->dat.file))) {
		return ir_err;
	}

	return concat_str(wk, dest, buf) == true ? ir_cont : ir_err;
}

static enum iteration_result
write_custom_tgt(struct workspace *wk, void *_ctx, uint32_t tgt_id)
{
	struct output *output = ((struct write_tgt_ctx *)_ctx)->output;

	struct obj *tgt = get_obj(wk, tgt_id);
	LOG_I("writing rules for custom target '%s'", wk_str(wk, tgt->dat.custom_target.name));

	uint32_t outputs, inputs = 0, cmdline_pre, cmdline = 0;

	if (!concat_strings(wk, tgt->dat.custom_target.input, &inputs)) {
		return ir_err;
	}

	outputs = wk_str_push(wk, "");
	if (!obj_array_foreach(wk, tgt->dat.custom_target.output, &outputs, custom_tgt_outputs_iter)) {
		return ir_err;
	}

	if (tgt->dat.custom_target.flags & custom_target_capture) {
		cmdline_pre = wk_str_pushf(wk, "%s internal exe ", wk->argv0);

		wk_str_app(wk, &cmdline_pre, "-c ");

		uint32_t elem;
		if (!obj_array_index(wk, tgt->dat.custom_target.output, 0, &elem)) {
			return ir_err;
		}

		if (custom_tgt_outputs_iter(wk, &cmdline_pre, elem) == ir_err) {
			return ir_err;
		}

		wk_str_app(wk, &cmdline_pre, "--");
	} else {
		cmdline_pre = wk_str_push(wk, "");
	}

	if (!concat_strings(wk, tgt->dat.custom_target.args, &cmdline)) {
		return ir_err;
	}

	fprintf(output->build_ninja, "build %s: CUSTOM_COMMAND %s | %s\n"
		" COMMAND = %s %s\n"
		" DESCRIPTION = %s%s\n\n",
		wk_str(wk, outputs),
		wk_str(wk, inputs),
		wk_objstr(wk, tgt->dat.custom_target.cmd),

		wk_str(wk, cmdline_pre),
		wk_str(wk, cmdline),
		wk_str(wk, cmdline),
		tgt->dat.custom_target.flags & custom_target_capture ? "(captured)": ""
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

static enum iteration_result
write_test_args_iter(struct workspace *wk, void *_ctx, uint32_t arg)
{
	struct write_tgt_ctx *ctx = _ctx;
	fputc(0, ctx->output->tests);

	uint32_t str;
	if (!strobj(wk, &str, arg)) {
		return ir_err;
	}

	fputs(wk_str(wk, str), ctx->output->tests);
	return ir_cont;
}

static enum iteration_result
write_test_iter(struct workspace *wk, void *_ctx, uint32_t test)
{
	struct write_tgt_ctx *ctx = _ctx;
	struct obj *t = get_obj(wk, test);

	uint32_t test_flags = 0;

	if (t->dat.test.should_fail) {
		test_flags |= test_flag_should_fail;
	}

	fs_fwrite(&test_flags, sizeof(uint32_t), ctx->output->tests);

	fputs(wk_objstr(wk, t->dat.test.name), ctx->output->tests);
	fputc(0, ctx->output->tests);
	fputs(wk_objstr(wk, t->dat.test.exe), ctx->output->tests);

	if (t->dat.test.args) {
		if (!obj_array_foreach_flat(wk, t->dat.test.args, ctx, write_test_args_iter)) {
			LOG_E("failed to write test '%s'", wk_objstr(wk, t->dat.test.name));
			return ir_err;
		}
	}
	fputc(0, ctx->output->tests);
	fputc(0, ctx->output->tests);

	return ir_cont;
}

static bool
write_project(struct output *output, struct workspace *wk, struct project *proj)
{
	struct write_tgt_ctx ctx = { .output = output, .proj = proj };

	if (!obj_array_foreach(wk, proj->targets, &ctx, write_tgt_iter)) {
		return false;
	}

	LOG_I("writing tests");

	if (!obj_array_foreach(wk, proj->tests, &ctx, write_test_iter)) {
		return false;
	}

	return true;
}

static bool
write_opts(FILE *f, struct workspace *wk)
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

static FILE *
open_out(const char *dir, const char *name)
{
	char path[PATH_MAX];
	if (!path_join(path, PATH_MAX, dir, name)) {
		return NULL;
	}

	return fs_fopen(path, "w");
}

bool
output_private_file(struct workspace *wk, char dest[PATH_MAX],
	const char *path, const char *txt)
{
	if (!path_join(dest, PATH_MAX, wk->muon_private, path)) {
		return false;
	}

	return fs_write(dest, (uint8_t *)txt, strlen(txt));
}

bool
output_build(struct workspace *wk)
{
	struct output output = { 0 };

	if (!(output.build_ninja = open_out(wk->build_root, "build.ninja"))) {
		return false;
	} else if (!(output.tests = open_out(wk->muon_private, outpath.tests))) {
		return false;
	} else if (!(output.opts = open_out(wk->muon_private, outpath.setup))) {
		return false;
	}

	write_hdr(output.build_ninja, wk, darr_get(&wk->projects, 0));
	write_opts(output.opts, wk);

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		if (!write_project(&output, wk, darr_get(&wk->projects, i))) {
			return false;
		}
	}

	if (!fs_fclose(output.build_ninja)) {
		return false;
	} else if (!fs_fclose(output.tests)) {
		return false;
	} else if (!fs_fclose(output.opts)) {
		return false;
	}

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
