/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "args.h"
#include "backend/common_args.h"
#include "backend/ninja.h"
#include "backend/ninja/build_target.h"
#include "error.h"
#include "functions/build_target.h"
#include "lang/object_iterators.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"

struct write_tgt_source_ctx {
	FILE *out;
	const struct obj_build_target *tgt;
	const struct project *proj;
	struct build_dep args;
	obj joined_args;
	obj joined_args_pch;
	obj object_names;
	obj order_deps;
	obj implicit_deps;
	bool have_order_deps;
	bool have_link_language;
};

enum write_tgt_src_flag {
	write_tgt_src_flag_pch = 1 << 0,
};

static obj
write_tgt_source(struct workspace *wk, struct write_tgt_source_ctx *ctx, enum compiler_language lang, obj val, enum write_tgt_src_flag flags)
{
	/* build paths */
	TSTR(dest_path);
	if ((flags & write_tgt_src_flag_pch)) {
		if (!tgt_src_to_pch_path(wk, ctx->tgt, lang, val, &dest_path)) {
			return 0;
		}
	} else {
		if (!tgt_src_to_object_path(wk, ctx->tgt, lang, val, true, &dest_path)) {
			return 0;
		}
	}

	obj dest = tstr_into_str(wk, &dest_path);

	if ((flags & write_tgt_src_flag_pch)) {
		if (get_obj_type(wk, val) == obj_build_target) {
			return dest;
		}
	} else {
		obj_array_push(wk, ctx->object_names, dest);
	}

	TSTR(src_path);
	const char *src = get_file_path(wk, val);
	path_relative_to(wk, &src_path, wk->build_root, src);

	/* build rules and args */

	obj rule_name, specialized_rule;
	if (flags & write_tgt_src_flag_pch) {
		if (!obj_dict_geti(wk, ctx->proj->generic_rules[ctx->tgt->machine], lang, &rule_name)) {
			UNREACHABLE;
		}
		specialized_rule = 0;
	} else {
		obj rule_name_arr;
		if (!obj_dict_geti(wk, ctx->tgt->required_compilers, lang, &rule_name_arr)) {
			UNREACHABLE;
		}

		rule_name = obj_array_index(wk, rule_name_arr, 0);
		specialized_rule = obj_array_index(wk, rule_name_arr, 1);
	}

	TSTR(esc_dest_path);
	TSTR(esc_path);

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
		obj *joined_args, processed_args;
		if (flags & write_tgt_src_flag_pch) {
			processed_args = ctx->tgt->processed_args_pch;
			joined_args = &ctx->joined_args_pch;
		} else {
			processed_args = ctx->tgt->processed_args;
			joined_args = &ctx->joined_args;
		}

		if (!*joined_args) {
			*joined_args = ca_build_target_joined_args(wk, processed_args);
		}

		obj args;
		if (!obj_dict_geti(wk, *joined_args, lang, &args)) {
			LOG_E("No compiler defined for language %s", compiler_language_to_s(lang));
			return 0;
		}

		fprintf(ctx->out, " ARGS = %s\n", get_cstr(wk, args));
	}

	return dest;
}

bool
ninja_write_build_tgt(struct workspace *wk, obj tgt_id, struct write_tgt_ctx *wctx)
{
	struct obj_build_target *tgt = get_obj_build_target(wk, tgt_id);
	L("writing rules for target '%s'", get_cstr(wk, tgt->build_name));

	TSTR(esc_path);
	TSTR(rel_build_path);
	{
		path_relative_to(wk, &rel_build_path, wk->build_root, get_cstr(wk, tgt->build_path));
		ninja_escape(wk, &esc_path, rel_build_path.buf);
	}

	struct write_tgt_source_ctx ctx = {
		.tgt = tgt,
		.proj = wctx->proj,
		.out = wctx->out,
	};

	ctx.object_names = make_obj(wk, obj_array);

	ctx.args = tgt->dep_internal;

	obj implicit_deps = 0;

	bool have_custom_order_deps = false;
	{ /* order deps */
		if ((ctx.have_order_deps = (get_obj_array(wk, ctx.args.order_deps)->len > 0))) {
			obj deduped;
			obj_array_dedup(wk, ctx.args.order_deps, &deduped);
			obj order_deps = join_args_ninja(wk, deduped);

			if (get_obj_array(wk, deduped)->len > 1) {
				fprintf(wctx->out,
					"build %s-order_deps: phony || %s\n",
					esc_path.buf,
					get_cstr(wk, order_deps));
				ctx.have_order_deps = false;
				implicit_deps = make_obj(wk, obj_array);
				obj_array_push(wk, implicit_deps, make_strf(wk, "%s-order_deps", esc_path.buf));
				have_custom_order_deps = true;
			} else {
				ctx.order_deps = order_deps;
			}
		}
	}

	if (implicit_deps) {
		ctx.implicit_deps = join_args_ninja(wk, implicit_deps);
	}

	if (tgt->pch) { /* pch */
		obj k, v;
		obj_dict_for(wk, tgt->pch, k, v) {
			enum compiler_language lang = k;

			if (!implicit_deps) {
				implicit_deps = make_obj(wk, obj_array);
			}
			obj src = write_tgt_source(wk, &ctx, lang, v, write_tgt_src_flag_pch);
			if (!src) {
				UNREACHABLE;
			}
			obj_array_push(wk, implicit_deps, src);
		}

		ctx.implicit_deps = join_args_ninja(wk, implicit_deps);
	}

	{ /* sources */
		obj v;
		obj_array_for(wk, tgt->objects, v)
		{
			const char *src = get_file_path(wk, v);

			TSTR(path);
			path_relative_to(wk, &path, wk->build_root, src);
			obj_array_push(wk, ctx.object_names, tstr_into_str(wk, &path));
		}

		obj_array_for(wk, tgt->src, v) {
			enum compiler_language lang;
			if (!filename_to_compiler_language(get_file_path(wk, v), &lang)) {
				UNREACHABLE;
			}

			if (!write_tgt_source(wk, &ctx, lang, v, 0)) {
				return false;
			}
		}
	}

	obj implicit_link_deps;
	implicit_link_deps = make_obj(wk, obj_array);
	if (have_custom_order_deps) {
		obj_array_push(wk, implicit_link_deps, make_strf(wk, "%s-order_deps", rel_build_path.buf));
	}

	if (!(tgt->type & tgt_static_library)) {
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

	const char *linker_type, *link_args;
	switch (tgt->type) {
	case tgt_shared_module:
	case tgt_dynamic_library:
	case tgt_executable:
		linker_type = compiler_language_to_s(tgt->dep_internal.link_language);
		link_args = get_cstr(wk, join_args_shell_ninja(wk, ctx.args.link_args));
		break;
	case tgt_static_library:
		if (!get_obj_array(wk, ctx.object_names)->len) {
			goto done;
		}
		linker_type = "static";
		link_args = 0;
		break;
	default: UNREACHABLE_RETURN;
	}

	fprintf(wctx->out, "build %s", esc_path.buf);

	if (tgt->implib) {
		obj rel;
		ca_relativize_path(wk, tgt->implib, true, &rel);
		fprintf(wctx->out, " | %s", get_cstr(wk, rel));
	}

	fprintf(wctx->out,
		": %s_%s_%s_linker ",
		get_cstr(wk, ctx.proj->rule_prefix),
		machine_kind_to_s(tgt->machine),
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
	if (link_args) {
		fprintf(wctx->out, "\n LINK_ARGS = %s", link_args);
	}

	if (tgt->flags & build_tgt_flag_build_by_default) {
		wctx->wrote_default = true;
		fprintf(wctx->out, "\ndefault %s\n", esc_path.buf);
	}

done:
	fprintf(wctx->out, "\n");
	return true;
}
