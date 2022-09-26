#include "posix.h"

#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "backend/ninja/rules.h"
#include "backend/output.h"
#include "error.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"
#include "tracy.h"

struct write_compiler_rule_ctx {
	FILE *out;
	struct project *proj;
	struct obj_build_target *tgt;
	obj args, generic_rules;
};

static void
uniqify_name(struct workspace *wk, obj arr, obj name, obj *res)
{
	uint32_t x = 1;
	while (obj_array_in(wk, arr, name)) {
		name = make_strf(wk, "%s%d", get_cstr(wk, name), x);
		++x;
	}

	obj_array_push(wk, arr, name);
	*res = name;
}

static enum iteration_result
write_linker_rule_iter(struct workspace *wk, void *_ctx, enum compiler_language l, obj comp_id)
{
	struct write_compiler_rule_ctx *ctx = _ctx;
	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	enum compiler_type t = comp->type;

	obj args;
	make_obj(wk, &args, obj_array);
	obj_array_extend(wk, args, comp->cmd_arr);
	obj_array_push(wk, args, make_str(wk, "$ARGS"));

	push_args(wk, args, compilers[t].args.output("$out"));
	obj_array_push(wk, args, make_str(wk, "$in"));
	obj_array_push(wk, args, make_str(wk, "$LINK_ARGS"));

	obj link_command = join_args_plain(wk, args);

	fprintf(ctx->out, "rule %s_%s_linker\n"
		" command = %s\n"
		" description = linking $out\n\n",
		get_cstr(wk, ctx->proj->rule_prefix),
		compiler_language_to_s(l),
		get_cstr(wk, link_command));

	return ir_cont;
}

static void
write_compiler_rule(struct workspace *wk, FILE *out, obj rule_args, obj rule_name, enum compiler_language l, obj comp_id)
{
	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	enum compiler_type t = comp->type;

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

	obj args;
	make_obj(wk, &args, obj_array);
	obj_array_extend(wk, args, comp->cmd_arr);
	obj_array_push(wk, args, rule_args);

	if (compilers[t].deps) {
		push_args(wk, args, compilers[t].args.deps("$out", "${out}.d"));
	}
	push_args(wk, args, compilers[t].args.output("$out"));
	push_args(wk, args, compilers[t].args.compile_only());
	obj_array_push(wk, args, make_str(wk, "$in"));

	obj compile_command = join_args_plain(wk, args);

	fprintf(out, "rule %s\n"
		" command = %s\n",
		get_cstr(wk, rule_name),
		get_cstr(wk, compile_command));
	if (compilers[t].deps) {
		fprintf(out,
			" deps = %s\n"
			" depfile = ${out}.d\n",
			deps);
	}
	fprintf(out,
		" description = compiling %s $out\n\n",
		compiler_language_to_s(l));
}

static enum iteration_result
write_compiler_rule_iter(struct workspace *wk, void *_ctx, enum compiler_language l, obj comp_id)
{
	struct write_compiler_rule_ctx *ctx = _ctx;

	obj rule_name;
	{
		obj rule_name_arr;
		if (!obj_dict_geti(wk, ctx->tgt->required_compilers, l, &rule_name_arr)) {
			return ir_cont;
		}

		obj specialized_rule;
		obj_array_index(wk, rule_name_arr, 0, &rule_name);
		obj_array_index(wk, rule_name_arr, 1, &specialized_rule);

		if (!specialized_rule) {
			return ir_cont;
		}
	}

	obj rule_args;
	if (!obj_dict_geti(wk, ctx->args, l, &rule_args)) {
		UNREACHABLE;
	}

	write_compiler_rule(wk, ctx->out, rule_args, rule_name, l, comp_id);
	return ir_cont;
}

static enum iteration_result
write_compiler_rule_tgt_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct write_compiler_rule_ctx *ctx = _ctx;
	enum iteration_result ret = ir_err;

	if (get_obj_type(wk, tgt_id) != obj_build_target) {
		return ir_cont;
	}

	struct obj_clear_mark mk;
	obj_set_clear_mark(wk, &mk);

	ctx->tgt = get_obj_build_target(wk, tgt_id);

	if (!build_target_args(wk, ctx->proj, ctx->tgt, &ctx->args)) {
		goto ret;
	}

	if (!obj_dict_foreach(wk, ctx->proj->compilers, ctx, write_compiler_rule_iter)) {
		goto ret;
	}

	ret = ir_cont;
ret:
	obj_clear(wk, &mk);
	return ret;
}

static enum iteration_result
write_generic_compiler_rule_iter(struct workspace *wk, void *_ctx, enum compiler_language l, obj comp_id)
{
	struct write_compiler_rule_ctx *ctx = _ctx;
	obj rule_name;

	if (!obj_dict_geti(wk, ctx->generic_rules, l, &rule_name)) {
		return ir_cont;
	}

	write_compiler_rule(wk, ctx->out, make_str(wk, "$ARGS"), rule_name, l, comp_id);
	return ir_cont;
}

struct name_compiler_rule_ctx {
	struct project *proj;
	struct obj_build_target *tgt;
	obj rule_prefix_arr;
	obj compiler_rule_arr;
	obj generic_rules;
};

static enum iteration_result
name_compiler_rule_iter(struct workspace *wk, void *_ctx, enum compiler_language l, uint32_t count)
{
	struct name_compiler_rule_ctx *ctx = _ctx;
	bool specialized_rule = count > 2;

	obj rule_name;
	if (specialized_rule) {
		obj name = make_strf(wk, "%s_%s_compiler_for_%s",
			get_cstr(wk, ctx->proj->rule_prefix),
			compiler_language_to_s(l),
			get_cstr(wk, ctx->tgt->build_name)
			);

		uniqify_name(wk, ctx->compiler_rule_arr, name, &rule_name);
	} else {
		if (!obj_dict_geti(wk, ctx->generic_rules, l, &rule_name)) {
			obj name = make_strf(wk, "%s_%s_compiler",
				get_cstr(wk, ctx->proj->rule_prefix),
				compiler_language_to_s(l));

			uniqify_name(wk, ctx->compiler_rule_arr, name, &rule_name);
			obj_dict_seti(wk, ctx->generic_rules, l, rule_name);
		}
	}

	obj arr;
	make_obj(wk, &arr, obj_array);
	obj_array_push(wk, arr, rule_name);
	obj_array_push(wk, arr, specialized_rule);

	obj_dict_seti(wk, ctx->tgt->required_compilers, l, arr);
	return ir_cont;
}

static enum iteration_result
name_compiler_rule_tgt_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct name_compiler_rule_ctx *ctx = _ctx;

	if (get_obj_type(wk, tgt_id) != obj_build_target) {
		return ir_cont;
	}

	ctx->tgt = get_obj_build_target(wk, tgt_id);

	if (!obj_dict_foreach(wk, ctx->tgt->required_compilers, ctx, name_compiler_rule_iter)) {
		return ir_err;
	}

	return ir_cont;
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
	case obj_string:
		sval = get_cstr(wk, o->val);
		break;
	case obj_array: {
		obj joined;
		obj_array_join(wk, true, o->val, make_str(wk, ","), &joined);
		sval = get_cstr(wk, joined);
		break;
	}
	default:
		UNREACHABLE;
	}

	obj_array_push(wk, regen_args, make_strf(wk, "-D%s=%s", get_cstr(wk, o->name), sval));
	return ir_cont;
}

bool
ninja_write_rules(FILE *out, struct workspace *wk, struct project *main_proj,
	bool need_phony,
	obj compiler_rule_arr)
{
	TracyCZoneAutoS;
	bool res = false;

	fprintf(
		out,
		"# This is the build file for project \"%s\"\n"
		"# It is autogenerated by the muon build system.\n"
		"ninja_required_version = 1.7.1\n\n",
		get_cstr(wk, main_proj->cfg.name)
		);

	fprintf(out,
		"rule static_linker\n"
		" command = rm -f $out && ar $LINK_ARGS $out $in\n"
		" description = linking static $out\n"
		"\n"
		"rule CUSTOM_COMMAND\n"
		" command = $COMMAND\n"
		" description = $DESCRIPTION\n"
		" restat = 1\n"
		"\n"
		);

	obj regen_args;
	make_obj(wk, &regen_args, obj_array);

	obj_array_push(wk, regen_args, make_str(wk, wk->argv0));
	obj_array_push(wk, regen_args, make_str(wk, "-C"));
	obj_array_push(wk, regen_args, make_str(wk, wk->source_root));
	obj_array_push(wk, regen_args, make_str(wk, "setup"));

	SBUF(compiler_check_cache_path);
	path_join(wk, &compiler_check_cache_path,
		wk->muon_private, output_path.compiler_check_cache);

	obj_array_push(wk, regen_args, make_str(wk, "-c"));
	obj_array_push(wk, regen_args, make_str(wk, compiler_check_cache_path.buf));

	obj_dict_foreach(wk, wk->global_opts, &regen_args, add_global_opts_set_from_env_iter);

	uint32_t i;
	for (i = 0; i < wk->original_commandline.argc; ++i) {
		obj_array_push(wk, regen_args,
			make_str(wk, wk->original_commandline.argv[i]));
	}

	obj regen_cmd = join_args_shell(wk, regen_args);

	fprintf(out,
		"rule REGENERATE_BUILD\n"
		" command = %s", get_cstr(wk, regen_cmd));

	fputs("\n description = Regenerating build files.\n"
		" generator = 1\n"
		"\n", out);

	obj regenerate_deps_rel;
	{
		obj deduped;
		obj_array_dedup(wk, wk->regenerate_deps, &deduped);
		relativize_paths(wk, deduped, true, &regenerate_deps_rel);
	}

	fprintf(out,
		"build build.ninja: REGENERATE_BUILD %s\n"
		" pool = console\n\n",
		get_cstr(wk, join_args_ninja(wk, regenerate_deps_rel))
		);

	if (need_phony) {
		fprintf(out, "build build_always_stale: phony\n\n");
	}

	obj rule_prefix_arr;
	make_obj(wk, &rule_prefix_arr, obj_array);
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = darr_get(&wk->projects, i);
		if (proj->not_ok) {
			continue;
		}

		TracyCZoneN(tctx_name, "name rules", true);

		{ // determine project rule prefix
			const char *proj_name = get_cstr(wk, proj->cfg.name);
			char buf[BUF_SIZE_1k] = { 0 }, *p;
			strncpy(buf, proj_name, BUF_SIZE_1k - 1);
			for (p = buf; *p; ++p) {
				if (*p == '_'
				    || ('a' <= *p && *p <= 'z')
				    || ('A' <= *p && *p <= 'Z')
				    || ('0' <= *p && *p <= '9')) {
					continue;
				}

				*p = '_';
			}

			obj pre = make_str(wk, buf);

			uniqify_name(wk, rule_prefix_arr, pre, &proj->rule_prefix);
		}

		obj generic_rules;
		make_obj(wk, &generic_rules, obj_dict);

		{
			struct name_compiler_rule_ctx ctx = {
				.proj = proj,
				.rule_prefix_arr = rule_prefix_arr,
				.compiler_rule_arr = compiler_rule_arr,
				.generic_rules = generic_rules,
			};

			if (!obj_array_foreach(wk, proj->targets, &ctx, name_compiler_rule_tgt_iter)) {
				goto ret;
			}
		}

		TracyCZoneEnd(tctx_name);

		{
			TracyCZoneN(tctx_rules, "write rules", true);

			struct write_compiler_rule_ctx ctx = {
				.out = out,
				.proj = proj,
				.generic_rules = generic_rules,
			};

			struct obj_clear_mark mk;
			obj_set_clear_mark(wk, &mk);

			if (!obj_array_foreach(wk, proj->targets, &ctx, write_compiler_rule_tgt_iter)) {
				goto ret;
			}

			if (!obj_dict_foreach(wk, proj->compilers, &ctx, write_generic_compiler_rule_iter)) {
				goto ret;
			}

			if (!obj_dict_foreach(wk, proj->compilers, &ctx, write_linker_rule_iter)) {
				goto ret;
			}

			obj_clear(wk, &mk);

			TracyCZoneEnd(tctx_rules);
		}
	}

	fprintf(out, "# targets\n\n");

	res = true;
ret:
	TracyCZoneAutoE;
	return res;
}
