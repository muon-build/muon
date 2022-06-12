#include "posix.h"

#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "backend/ninja.h"
#include "backend/ninja/build_target.h"
#include "error.h"
#include "functions/build_target.h"
#include "functions/default/dependency.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

struct write_tgt_iter_ctx {
	FILE *out;
	const struct obj_build_target *tgt;
	const struct project *proj;
	struct build_dep args;
	obj joined_args;
	obj object_names;
	obj order_deps;
	bool have_order_deps;
	bool have_link_language;
};

static enum iteration_result
add_tgt_objects_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	const char *src = get_file_path(wk, val);

	char path[PATH_MAX];
	if (!path_relative_to(path, PATH_MAX, wk->build_root, src)) {
		return ir_err;
	}

	obj_array_push(wk, ctx->object_names, make_str(wk, path));
	return ir_cont;
}

static enum iteration_result
write_tgt_sources_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	const char *src = get_file_path(wk, val);

	enum compiler_language lang;
	enum compiler_type ct;

	{
		obj comp_id;

		if (!filename_to_compiler_language(src, &lang)) {
			UNREACHABLE;
		}

		if (!obj_dict_geti(wk, ctx->proj->compilers, lang, &comp_id)) {
			LOG_E("no compiler for '%s'", compiler_language_to_s(lang));
			return ir_err;
		}

		ct = get_obj_compiler(wk, comp_id)->type;
	}

	/* build paths */
	char dest_path[PATH_MAX];
	if (!tgt_src_to_object_path(wk, ctx->tgt, val, true, dest_path)) {
		return ir_err;
	}

	char src_path[PATH_MAX];
	if (!path_relative_to(src_path, PATH_MAX, wk->build_root, src)) {
		return ir_err;
	}

	obj_array_push(wk, ctx->object_names, make_str(wk, dest_path));

	/* build rules and args */

	obj args_id;
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

	fprintf(ctx->out, "build %s: %s%s_COMPILER %s", esc_dest_path,
		get_cstr(wk, ctx->proj->rule_prefix), compiler_language_to_s(lang), esc_path);
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

bool
ninja_write_build_tgt(struct workspace *wk, obj tgt_id, struct write_tgt_ctx *wctx)
{
	struct obj_build_target *tgt = get_obj_build_target(wk, tgt_id);
	LOG_I("writing rules for target '%s'", get_cstr(wk, tgt->build_name));

	struct write_tgt_iter_ctx ctx = {
		.tgt = tgt,
		.proj = wctx->proj,
		.out = wctx->out,
	};

	enum linker_type linker;
	{ /* determine linker */
		obj comp_id;
		if (!obj_dict_geti(wk, ctx.proj->compilers, tgt->dep.link_language, &comp_id)) {
			LOG_E("no compiler defined for language %s", compiler_language_to_s(tgt->dep.link_language));
			return false;
		}

		linker = compilers[get_obj_compiler(wk, comp_id)->type].linker;
	}

	make_obj(wk, &ctx.object_names, obj_array);

	ctx.args = tgt->dep;

	if (!relativize_paths(wk, ctx.args.link_with, true, &ctx.args.link_with)) {
		return false;
	}

	if (tgt->flags & build_tgt_generated_include) {
		const char *private_path = get_cstr(wk, tgt->private_path);

		// mkdir so that the include dir doesn't get pruned later on
		if (!fs_mkdir_p(private_path)) {
			return false;
		}

		obj inc;
		make_obj(wk, &inc, obj_array);
		obj_array_push(wk, inc, make_str(wk, private_path));
		obj_array_extend_nodup(wk, inc, ctx.args.include_directories);
		ctx.args.include_directories = inc;
	}

	{ /* order deps */
		if ((ctx.have_order_deps = get_obj_array(wk, ctx.args.order_deps)->len)) {
			ctx.order_deps = join_args_ninja(wk, ctx.args.order_deps);
		}
	}

	if (!setup_compiler_args(wk, ctx.tgt, ctx.proj, ctx.args.include_directories, ctx.args.compile_args, &ctx.joined_args)) {
		return false;
	}

	{ /* sources */
		obj_array_foreach(wk, tgt->objects, &ctx, add_tgt_objects_iter);

		if (!obj_array_foreach(wk, tgt->src, &ctx, write_tgt_sources_iter)) {
			return false;
		}
	}

	if (tgt->type & (tgt_dynamic_library | tgt_shared_module)) {
		push_args(wk, ctx.args.link_args, linkers[linker].args.shared());
		push_args(wk, ctx.args.link_args, linkers[linker].args.soname(get_cstr(wk, tgt->soname)));
		if (tgt->type == tgt_shared_module) {
			push_args(wk, ctx.args.link_args, linkers[linker].args.allow_shlib_undefined());
		}
	}

	obj implicit_deps;
	make_obj(wk, &implicit_deps, obj_array);
	if (!(tgt->type & (tgt_static_library))) {
		struct setup_linker_args_ctx sctx = {
			.linker = linker,
			.link_lang = tgt->dep.link_language,
			.args = &ctx.args
		};

		setup_linker_args(wk, ctx.proj, tgt, &sctx);

		if (get_obj_array(wk, ctx.args.link_with)->len) {
			obj_array_extend(wk, implicit_deps, ctx.args.link_with);
		}
	}

	if (tgt->link_depends) {
		obj arr;
		if (!arr_to_args(wk, arr_to_args_relativize_paths, tgt->link_depends, &arr)) {
			return false;
		}

		obj_array_extend_nodup(wk, implicit_deps, arr);
	}

	const char *linker_type, *link_args;
	bool linker_rule_prefix = false;
	switch (tgt->type) {
	case tgt_shared_module:
	case tgt_dynamic_library:
	case tgt_executable:
		linker_type = compiler_language_to_s(tgt->dep.link_language);
		linker_rule_prefix = true;
		link_args = get_cstr(wk, join_args_shell_ninja(wk, ctx.args.link_args));
		break;
	case tgt_static_library:
		linker_type = "STATIC";
		link_args = linker != linker_apple ? "csrD" : "csr";
		break;
	default:
		assert(false);
		return false;
	}

	char esc_path[PATH_MAX];
	{
		char rel_build_path[PATH_MAX];
		if (!path_relative_to(rel_build_path, PATH_MAX, wk->build_root, get_cstr(wk, tgt->build_path))) {
			return false;
		}

		if (!ninja_escape(esc_path, PATH_MAX, rel_build_path)) {
			return false;
		}
	}

	fprintf(wctx->out, "build %s: %s%s_LINKER ", esc_path,
		linker_rule_prefix ? get_cstr(wk, ctx.proj->rule_prefix) : "",
		linker_type);
	fputs(get_cstr(wk, join_args_ninja(wk, ctx.object_names)), wctx->out);
	if (get_obj_array(wk, implicit_deps)->len) {
		implicit_deps = join_args_ninja(wk, implicit_deps);
		fputs(" | ", wctx->out);
		fputs(get_cstr(wk, implicit_deps), wctx->out);
	}
	if (ctx.have_order_deps) {
		fputs(" || ", wctx->out);
		fputs(get_cstr(wk, ctx.order_deps), wctx->out);
	}
	fprintf(wctx->out, "\n LINK_ARGS = %s\n", link_args);

	if (tgt->flags & build_tgt_flag_build_by_default) {
		wctx->wrote_default = true;
		fprintf(wctx->out, "default %s\n", esc_path);
	}
	fprintf(wctx->out, "\n");
	return true;
}
