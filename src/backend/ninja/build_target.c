#include "posix.h"

#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "backend/ninja/build_target.h"
#include "functions/build_target.h"
#include "functions/dependency.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"

struct write_tgt_iter_ctx {
	FILE *out;
	const struct obj *tgt;
	const struct project *proj;
	struct dep_args_ctx args;
	obj joined_args;
	obj object_names;
	obj order_deps;
	bool have_order_deps;
	bool have_link_language;
	enum compiler_language link_language;
};

static enum iteration_result
write_tgt_sources_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	struct obj *src = get_obj(wk, val_id);
	assert(src->type == obj_file);

	enum compiler_language lang;
	enum compiler_type ct;

	{
		uint32_t comp_id;

		// TODO put these checks into tgt creation
		if (!filename_to_compiler_language(get_cstr(wk, src->dat.file), &lang)) {
			/* LOG_E("unable to determine language for '%s'", get_cstr(wk, src->dat.file)); */
			return ir_cont;
		}

		if (languages[lang].is_header) {
			return ir_cont;
		} else if (languages[lang].is_linkable) {
			char path[PATH_MAX];
			if (!path_relative_to(path, PATH_MAX, wk->build_root, get_cstr(wk, src->dat.file))) {
				return ir_err;
			}
			obj_array_push(wk, ctx->object_names, make_str(wk, path));
			return ir_cont;
		}

		if (!obj_dict_geti(wk, ctx->proj->compilers, lang, &comp_id)) {
			LOG_E("no compiler for '%s'", compiler_language_to_s(lang));
			return ir_err;
		}

		ct = get_obj(wk, comp_id)->dat.compiler.type;
	}

	/* build paths */
	char dest_path[PATH_MAX];
	if (!tgt_src_to_object_path(wk, ctx->tgt, val_id, true, dest_path)) {
		return ir_err;
	}

	char src_path[PATH_MAX];
	if (!path_relative_to(src_path, PATH_MAX, wk->build_root, get_cstr(wk, src->dat.file))) {
		return ir_err;
	}

	obj_array_push(wk, ctx->object_names, make_str(wk, dest_path));

	/* build rules and args */

	uint32_t args_id;
	if (!obj_dict_geti(wk, ctx->joined_args, lang, &args_id)) {
		LOG_E("couldn't get args for language %s", compiler_language_to_s(lang));
		return ir_err;
	}

	char esc_dest_path[PATH_MAX], esc_path[PATH_MAX];
	if (!ninja_escape(esc_dest_path, PATH_MAX, dest_path)) {
		return false;
	} else if (!ninja_escape(esc_path, PATH_MAX, src_path)) {
		return false;
	}

	fprintf(ctx->out, "build %s: %s_COMPILER %s", esc_dest_path, compiler_language_to_s(lang), esc_path);
	if (ctx->have_order_deps) {
		fprintf(ctx->out, " || %s", get_cstr(wk, ctx->order_deps));
	}
	fputc('\n', ctx->out);

	fprintf(ctx->out,
		" ARGS = %s\n", get_cstr(wk, args_id));

	if (compilers[ct].deps) {
		if (!path_add_suffix(esc_dest_path, PATH_MAX, ".d")) {
			return false;
		}

		fprintf(ctx->out, " DEPFILE_UNQUOTED = %s\n", esc_dest_path);

		if (!shell_escape(esc_path, PATH_MAX, esc_dest_path)) {
			return false;
		}

		fprintf(ctx->out, " DEPFILE = %s\n", esc_path);
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

	if (filename_to_compiler_language(get_cstr(wk, src->dat.file), &fl)
	    && !languages[fl].is_header) {
		/* LOG_E("unable to determine language for '%s'", get_cstr(wk, src->dat.file)); */
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

	obj_array_push(wk, ctx->args.include_dirs, make_str(wk, dir));

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
		/* LOG_E("unable to determine language for '%s'", get_cstr(wk, src->dat.file)); */
		return ir_cont;
	}

	switch (fl) {
	case compiler_language_c_hdr:
	case compiler_language_cpp_hdr:
		return ir_cont;
	case compiler_language_c:
	case compiler_language_c_obj:
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

static bool
tgt_args(struct workspace *wk, const struct obj *tgt, struct dep_args_ctx *ctx)
{
	if (tgt->dat.tgt.include_directories) {
		if (!obj_array_foreach_flat(wk, tgt->dat.tgt.include_directories,
			ctx, dep_args_includes_iter)) {
			return false;
		}
	}

	if (tgt->dat.tgt.deps) {
		if (!deps_args(wk, tgt->dat.tgt.deps, ctx)) {
			return false;
		}
	}

	if (tgt->dat.tgt.link_with) {
		if (!obj_array_foreach(wk, tgt->dat.tgt.link_with, ctx, dep_args_link_with_iter)) {
			return false;
		}
	}

	if (tgt->dat.tgt.link_args) {
		obj arr;
		obj_array_dup(wk, tgt->dat.tgt.link_args, &arr);
		obj_array_extend(wk, ctx->link_args, arr);
	}
	return true;
}

bool
ninja_write_build_tgt(struct workspace *wk, const struct project *proj, obj tgt_id, FILE *out)
{
	struct obj *tgt = get_obj(wk, tgt_id);
	LOG_I("writing rules for target '%s'", get_cstr(wk, tgt->dat.tgt.build_name));

	char build_path[PATH_MAX];
	if (!tgt_build_path(wk, tgt, true, build_path)) {
		return ir_err;
	}

	struct write_tgt_iter_ctx ctx = {
		.tgt = tgt,
		.proj = proj,
		.out = out,
	};

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

	make_obj(wk, &ctx.object_names, obj_array);
	make_obj(wk, &ctx.order_deps, obj_array);

	dep_args_ctx_init(wk, &ctx.args);
	ctx.args.relativize = true;
	ctx.args.recursive = true;

	if (!tgt_args(wk, tgt, &ctx.args)) {
		return false;
	}

	/* sources includes */
	if (!obj_array_foreach(wk, tgt->dat.tgt.src, &ctx, process_source_includes_iter)) {
		return false;
	}

	if (!setup_compiler_args(wk, ctx.tgt, ctx.proj, ctx.args.include_dirs, ctx.args.compile_args, &ctx.joined_args)) {
		return false;
	}

	ctx.order_deps = join_args_ninja(wk, ctx.order_deps);

	{ /* sources */
		if (!obj_array_foreach(wk, tgt->dat.tgt.src, &ctx, write_tgt_sources_iter)) {
			return false;
		}
	}

	obj implicit_deps = 0;
	if (tgt->dat.tgt.type == tgt_executable) {
		obj link_with;
		obj_array_dedup(wk, ctx.args.link_with, &link_with);

		if (get_obj(wk, ctx.args.link_with)->dat.arr.len) {
			implicit_deps = str_join(wk, make_str(wk, " | "), join_args_ninja(wk, link_with));
		}

		setup_linker_args(wk, ctx.proj, tgt, linker, ctx.link_language,
			ctx.args.rpath, ctx.args.link_args, link_with);
	}

	const char *linker_type, *link_args;
	switch (tgt->dat.tgt.type) {
	case tgt_shared_module:
	case tgt_dynamic_library:
	case tgt_executable:
		linker_type = compiler_language_to_s(ctx.link_language);

		if (tgt->dat.tgt.type & (tgt_dynamic_library | tgt_shared_module)) {
			push_args(wk, ctx.args.link_args, linkers[linker].args.shared());
			push_args(wk, ctx.args.link_args, linkers[linker].args.soname(get_cstr(wk, tgt->dat.tgt.soname)));

			if (tgt->dat.tgt.type == tgt_shared_module) {
				push_args(wk, ctx.args.link_args, linkers[linker].args.allow_shlib_undefined());
			}
		}

		link_args = get_cstr(wk, join_args_shell_ninja(wk, ctx.args.link_args));
		break;
	case tgt_static_library:
		linker_type = "STATIC";
		link_args = "csrD";
		break;
	default:
		assert(false);
		return false;
	}

	char esc_path[PATH_MAX];
	if (!ninja_escape(esc_path, PATH_MAX, build_path)) {
		return false;
	}

	fprintf(out, "build %s: %s_LINKER ", esc_path, linker_type);
	fputs(get_cstr(wk, join_args_ninja(wk, ctx.object_names)), out);
	if (implicit_deps) {
		fputs(get_cstr(wk, implicit_deps), out);
	}
	if (ctx.have_order_deps) {
		fputs(" || ", out);
		fputs(get_cstr(wk, ctx.order_deps), out);
	}
	fprintf(out, "\n LINK_ARGS = %s\n\n", link_args);
	return true;
}
