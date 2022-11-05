/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "backend/ninja.h"
#include "backend/ninja/build_target.h"
#include "error.h"
#include "functions/build_target.h"
#include "functions/kernel/dependency.h"
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
	obj implicit_deps;
	bool have_order_deps;
	bool have_link_language;
};

static enum iteration_result
add_tgt_objects_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	const char *src = get_file_path(wk, val);

	SBUF(path);
	path_relative_to(wk, &path, wk->build_root, src);
	obj_array_push(wk, ctx->object_names, sbuf_into_str(wk, &path));
	return ir_cont;
}

static enum iteration_result
write_tgt_sources_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct write_tgt_iter_ctx *ctx = _ctx;
	const char *src = get_file_path(wk, val);

	enum compiler_language lang;
	if (!filename_to_compiler_language(src, &lang)) {
		UNREACHABLE;
	}

	/* build paths */
	SBUF(dest_path);
	if (!tgt_src_to_object_path(wk, ctx->tgt, val, true, &dest_path)) {
		return ir_err;
	}

	SBUF(src_path);
	path_relative_to(wk, &src_path, wk->build_root, src);

	obj_array_push(wk, ctx->object_names, sbuf_into_str(wk, &dest_path));

	/* build rules and args */

	obj rule_name, specialized_rule;
	{
		obj rule_name_arr;
		if (!obj_dict_geti(wk, ctx->tgt->required_compilers, lang, &rule_name_arr)) {
			UNREACHABLE;
		}

		obj_array_index(wk, rule_name_arr, 0, &rule_name);
		obj_array_index(wk, rule_name_arr, 1, &specialized_rule);

		if (!specialized_rule) {
			if (!ctx->joined_args && !build_target_args(wk, ctx->proj, ctx->tgt, &ctx->joined_args)) {
				return ir_err;
			}
		}
	}

	SBUF(esc_dest_path);
	SBUF(esc_path);

	ninja_escape(wk, &esc_dest_path, dest_path.buf);
	ninja_escape(wk, &esc_path, src_path.buf);

	fprintf(ctx->out, "build %s: %s %s", esc_dest_path.buf, get_cstr(wk, rule_name), esc_path.buf);
	if (ctx->implicit_deps) {
		fputs(" | ", ctx->out);
		fputs(get_cstr(wk, ctx->implicit_deps), ctx->out);
	}
	if (ctx->have_order_deps) {
		fprintf(ctx->out, " || %s", get_cstr(wk, ctx->order_deps));
	}
	fputc('\n', ctx->out);

	if (!specialized_rule) {
		obj args;
		if (!obj_dict_geti(wk, ctx->joined_args, lang, &args)) {
			UNREACHABLE;
		}

		fprintf(ctx->out,
			" ARGS = %s\n", get_cstr(wk, args));
	}

	return ir_cont;
}

bool
ninja_write_build_tgt(struct workspace *wk, obj tgt_id, struct write_tgt_ctx *wctx)
{
	struct obj_build_target *tgt = get_obj_build_target(wk, tgt_id);
	L("writing rules for target '%s'", get_cstr(wk, tgt->build_name));

	SBUF(esc_path);
	{
		SBUF(rel_build_path);
		path_relative_to(wk, &rel_build_path, wk->build_root, get_cstr(wk, tgt->build_path));
		ninja_escape(wk, &esc_path, rel_build_path.buf);
	}

	struct write_tgt_iter_ctx ctx = {
		.tgt = tgt,
		.proj = wctx->proj,
		.out = wctx->out,
	};

	enum linker_type linker;
	{ /* determine linker */
		obj comp_id;
		if (!obj_dict_geti(wk, ctx.proj->compilers, tgt->dep_internal.link_language, &comp_id)) {
			LOG_E("no compiler defined for language %s", compiler_language_to_s(tgt->dep_internal.link_language));
			return false;
		}

		linker = compilers[get_obj_compiler(wk, comp_id)->type].linker;
	}

	make_obj(wk, &ctx.object_names, obj_array);

	ctx.args = tgt->dep_internal;

	relativize_paths(wk, ctx.args.link_with, true, &ctx.args.link_with);
	relativize_paths(wk, ctx.args.link_whole, true, &ctx.args.link_whole);

	{ /* order deps */
		if ((ctx.have_order_deps = (get_obj_array(wk, ctx.args.order_deps)->len > 0))) {
			obj deduped;
			obj_array_dedup(wk, ctx.args.order_deps, &deduped);
			obj order_deps = join_args_ninja(wk, deduped);

			if (get_obj_array(wk, deduped)->len > 1) {
				fprintf(wctx->out, "build %s-order_deps: phony || %s\n", esc_path.buf, get_cstr(wk, order_deps));
				ctx.have_order_deps = false;
				ctx.implicit_deps = make_strf(wk, "%s-order_deps", esc_path.buf);
			} else {
				ctx.order_deps = order_deps;
			}
		}
	}

	{ /* sources */
		obj_array_foreach(wk, tgt->objects, &ctx, add_tgt_objects_iter);

		if (!obj_array_foreach(wk, tgt->src, &ctx, write_tgt_sources_iter)) {
			return false;
		}
	}

	obj implicit_link_deps;
	make_obj(wk, &implicit_link_deps, obj_array);
	if (ctx.implicit_deps) {
		obj_array_push(wk, implicit_link_deps, ctx.implicit_deps);
	}

	if (!(tgt->type & (tgt_static_library))) {
		struct setup_linker_args_ctx sctx = {
			.linker = linker,
			.link_lang = tgt->dep_internal.link_language,
			.args = &ctx.args
		};

		setup_linker_args(wk, ctx.proj, tgt, &sctx);

		if (get_obj_array(wk, ctx.args.link_with)->len) {
			obj_array_extend(wk, implicit_link_deps, ctx.args.link_with);
		}

		if (get_obj_array(wk, ctx.args.link_whole)->len) {
			obj_array_extend(wk, implicit_link_deps, ctx.args.link_whole);
		}
	}

	if (tgt->link_depends) {
		obj arr;
		if (!arr_to_args(wk, arr_to_args_relativize_paths, tgt->link_depends, &arr)) {
			return false;
		}

		obj_array_extend_nodup(wk, implicit_link_deps, arr);
	}

	if (tgt->type & (tgt_dynamic_library | tgt_shared_module)) {
		push_args(wk, ctx.args.link_args, linkers[linker].args.shared());
		push_args(wk, ctx.args.link_args, linkers[linker].args.soname(get_cstr(wk, tgt->soname)));
		if (tgt->type == tgt_shared_module) {
			push_args(wk, ctx.args.link_args, linkers[linker].args.allow_shlib_undefined());
		}
	}

	const char *linker_type, *link_args;
	bool linker_rule_prefix = false;
	switch (tgt->type) {
	case tgt_shared_module:
	case tgt_dynamic_library:
	case tgt_executable:
		linker_type = compiler_language_to_s(tgt->dep_internal.link_language);
		linker_rule_prefix = true;
		link_args = get_cstr(wk, join_args_shell_ninja(wk, ctx.args.link_args));
		break;
	case tgt_static_library:
		linker_type = "static";
		link_args = ar_arguments();
		break;
	default:
		assert(false);
		return false;
	}

	fprintf(wctx->out, "build %s: %s%s%s_linker ", esc_path.buf,
		linker_rule_prefix ? get_cstr(wk, ctx.proj->rule_prefix) : "",
		linker_rule_prefix ? "_" : "",
		linker_type);
	fputs(get_cstr(wk, join_args_ninja(wk, ctx.object_names)), wctx->out);
	if (get_obj_array(wk, implicit_link_deps)->len) {
		implicit_link_deps = join_args_ninja(wk, implicit_link_deps);
		fputs(" | ", wctx->out);
		fputs(get_cstr(wk, implicit_link_deps), wctx->out);
	}
	if (ctx.have_order_deps) {
		fputs(" || ", wctx->out);
		fputs(get_cstr(wk, ctx.order_deps), wctx->out);
	}
	fprintf(wctx->out, "\n LINK_ARGS = %s\n", link_args);

	if (tgt->flags & build_tgt_flag_build_by_default) {
		wctx->wrote_default = true;
		fprintf(wctx->out, "default %s\n", esc_path.buf);
	}
	fprintf(wctx->out, "\n");
	return true;
}
