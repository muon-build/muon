/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Owen Rafferty <owen@owenrafferty.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "error.h"
#include "functions/both_libs.h"
#include "functions/build_target.h"
#include "lang/object_iterators.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static void
ca_get_option_value_for_tgt(struct workspace *wk,
	const struct project *proj,
	const struct obj_build_target *tgt,
	const char *name,
	obj *res)
{
	get_option_value_overridable(wk, proj, tgt ? tgt->override_options : 0, name, res);
}

struct ca_buildtype {
	enum compiler_optimization_lvl opt;
	bool debug;
};

static void
ca_get_buildtype(struct workspace *wk,
	const struct project *proj,
	const struct obj_build_target *tgt,
	struct ca_buildtype *buildtype)
{
	uint32_t i;
	buildtype->opt = 0;
	buildtype->debug = false;

	static struct {
		const char *name;
		enum compiler_optimization_lvl opt;
		bool debug;
	} tbl[] = { { "plain", compiler_optimization_lvl_none, false },
		{ "debug", compiler_optimization_lvl_0, true },
		{ "debugoptimized", compiler_optimization_lvl_g, true },
		{ "release", compiler_optimization_lvl_3, false },
		{ "minsize", compiler_optimization_lvl_s, false },
		{ NULL } };

	obj buildtype_opt_id, buildtype_val;
	get_option_overridable(wk, proj, tgt ? tgt->override_options : 0, &STR("buildtype"), &buildtype_opt_id);
	struct obj_option *buildtype_opt = get_obj_option(wk, buildtype_opt_id);
	buildtype_val = buildtype_opt->val;

	const char *str = get_cstr(wk, buildtype_val);

	bool use_custom = (strcmp(str, "custom") == 0) || (buildtype_opt->source <= option_value_source_default);

	if (use_custom) {
		obj optimization_id, debug_id;

		ca_get_option_value_for_tgt(wk, proj, tgt, "optimization", &optimization_id);
		ca_get_option_value_for_tgt(wk, proj, tgt, "debug", &debug_id);

		const struct str *str = get_str(wk, optimization_id);
		if (str_eql(str, &STR("plain"))) {
			buildtype->opt = compiler_optimization_lvl_none;
		} else if (str->len != 1) {
			UNREACHABLE;
		}

		switch (*str->s) {
		case '0':
		case '1':
		case '2':
		case '3': buildtype->opt = compiler_optimization_lvl_0 + (*str->s - '0'); break;
		case 'g': buildtype->opt = compiler_optimization_lvl_g; break;
		case 's': buildtype->opt = compiler_optimization_lvl_s; break;
		default: UNREACHABLE;
		}

		buildtype->debug = get_obj_bool(wk, debug_id);
	} else {
		for (i = 0; tbl[i].name; ++i) {
			if (strcmp(str, tbl[i].name) == 0) {
				buildtype->opt = tbl[i].opt;
				buildtype->debug = tbl[i].debug;
				break;
			}
		}

		if (!tbl[i].name) {
			LOG_E("invalid build type %s", str);
			UNREACHABLE;
		}
	}
}

static void
ca_get_buildtype_args(struct workspace *wk, struct obj_compiler *comp, const struct ca_buildtype *buildtype, obj args_id)
{
	if (buildtype->debug) {
		push_args(wk, args_id, toolchain_compiler_debug(wk, comp));
	}

	push_args(wk, args_id, toolchain_compiler_optimization(wk, comp, buildtype->opt));
}

static void
ca_get_warning_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj lvl_id;
	ca_get_option_value_for_tgt(wk, proj, tgt, "warning_level", &lvl_id);
	const struct str *sl = get_str(wk, lvl_id);

	if (str_eql(sl, &STR("everything"))) {
		push_args(wk, args_id, toolchain_compiler_warn_everything(wk, comp));
		return;
	}

	uint32_t lvl;
	assert(sl->len == 1 && "invalid warning_level");
	switch (sl->s[0]) {
	case '0': lvl = 0; break;
	case '1': lvl = 1; break;
	case '2': lvl = 2; break;
	case '3': lvl = 3; break;
	default: UNREACHABLE; return;
	}

	push_args(wk, args_id, toolchain_compiler_warning_lvl(wk, comp, lvl));

	if (tgt->pch && lvl >= 1) {
		push_args(wk, args_id, toolchain_compiler_winvalid_pch(wk, comp));
	}
}

static void
ca_get_werror_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj active;
	ca_get_option_value_for_tgt(wk, proj, tgt, "werror", &active);

	if (get_obj_bool(wk, active)) {
		push_args(wk, args_id, toolchain_compiler_werror(wk, comp));
	}
}

void
ca_get_std_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj std;

	switch (comp->lang) {
	case compiler_language_objc:
	case compiler_language_c: ca_get_option_value_for_tgt(wk, proj, tgt, "c_std", &std); break;
	case compiler_language_objcpp:
	case compiler_language_cpp: ca_get_option_value_for_tgt(wk, proj, tgt, "cpp_std", &std); break;
	default: return;
	}

	const char *s, *next = get_cstr(wk, std);
	char buf[256];
	uint32_t len;

	do {
		s = next;
		if ((next = strchr(s, ','))) {
			len = next - s;
			++next;
		} else {
			len = strlen(s);
			next = s + len;
		}

		if (!len) {
			continue;
		} else if (len >= sizeof(buf)) {
			LOG_W("skipping invalid std '%.*s'", len, s);
		} else if (strncmp(s, "none", len) == 0) {
			return;
		} else {
			memcpy(buf, s, len);
			buf[len] = 0;

			if (toolchain_compiler_std_supported(wk, comp, buf)) {
				push_args(wk, args_id, toolchain_compiler_set_std(wk, comp, buf));
				return;
			}
		}
	} while (*next);

	LOG_W("none of the requested stds are supported: '%s'", get_cstr(wk, std));
}

void
ca_get_option_compile_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj args;
	switch (comp->lang) {
#define TOOLCHAIN_ENUM(lang) \
	case compiler_language_##lang: ca_get_option_value_for_tgt(wk, proj, tgt, #lang "_args", &args); break;
		FOREACH_COMPILER_EXPOSED_LANGUAGE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
	default: return;
	}

	obj_array_extend(wk, args_id, args);
}

static void
ca_setup_optional_b_args_compiler(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	const struct ca_buildtype *buildtype,
	obj args)
{
	obj opt;

	ca_get_option_value_for_tgt(wk, proj, tgt, "b_vscrt", &opt);
	push_args(wk, args, toolchain_compiler_crt(wk, comp, get_cstr(wk, opt), buildtype->debug));

	ca_get_option_value_for_tgt(wk, proj, tgt, "b_pgo", &opt);
	if (!str_eql(get_str(wk, opt), &STR("off"))) {
		uint32_t stage;
		const struct str *sl = get_str(wk, opt);
		if (str_eql(sl, &STR("generate"))) {
			stage = 0;
		} else if (str_eql(sl, &STR("use"))) {
			stage = 1;
		} else {
			UNREACHABLE;
			return;
		}
		push_args(wk, args, toolchain_compiler_pgo(wk, comp, stage));
	}

	ca_get_option_value_for_tgt(wk, proj, tgt, "b_sanitize", &opt);
	if (!str_eql(get_str(wk, opt), &STR("none"))) {
		push_args(wk, args, toolchain_compiler_sanitize(wk, comp, get_cstr(wk, opt)));
	}

	obj buildtype_val;
	ca_get_option_value_for_tgt(wk, proj, tgt, "buildtype", &buildtype_val);
	ca_get_option_value_for_tgt(wk, proj, tgt, "b_ndebug", &opt);
	if (str_eql(get_str(wk, opt), &STR("true"))
		|| (str_eql(get_str(wk, opt), &STR("if-release"))
			&& str_eql(get_str(wk, buildtype_val), &STR("release")))) {
		push_args(wk, args, toolchain_compiler_define(wk, comp, "NDEBUG"));
	}

	ca_get_option_value_for_tgt(wk, proj, tgt, "b_colorout", &opt);
	if (!str_eql(get_str(wk, opt), &STR("never"))) {
		push_args(wk, args, toolchain_compiler_color_output(wk, comp, get_cstr(wk, opt)));
	}

	ca_get_option_value_for_tgt(wk, proj, tgt, "b_lto", &opt);
	if (get_obj_bool(wk, opt)) {
		push_args(wk, args, toolchain_compiler_enable_lto(wk, comp));
	}

	ca_get_option_value_for_tgt(wk, proj, tgt, "b_coverage", &opt);
	if (get_obj_bool(wk, opt)) {
		push_args(wk, args, toolchain_compiler_coverage(wk, comp));
	}
}

static obj
ca_get_base_compiler_args(struct workspace *wk,
	const struct project *proj,
	const struct obj_build_target *tgt,
	enum compiler_language lang,
	obj comp_id)
{
	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	struct ca_buildtype buildtype;

	ca_get_buildtype(wk, proj, tgt, &buildtype);

	obj args;
	args = make_obj(wk, obj_array);

	push_args(wk, args, toolchain_compiler_always(wk, comp));

	ca_get_std_args(wk, comp, proj, tgt, args);
	ca_get_buildtype_args(wk, comp, &buildtype, args);
	ca_get_warning_args(wk, comp, proj, tgt, args);
	ca_get_werror_args(wk, comp, proj, tgt, args);

	ca_setup_optional_b_args_compiler(wk, comp, proj, tgt, &buildtype, args);

	{ /* option args (from option('x_args')) */
		ca_get_option_compile_args(wk, comp, proj, tgt, args);
	}

	{ /* global args */
		obj global_args;
		if (obj_dict_geti(wk, wk->global_args[tgt->machine], lang, &global_args)) {
			obj_array_extend(wk, args, global_args);
		}
	}

	{ /* project args */
		obj proj_args;
		if (obj_dict_geti(wk, proj->args[tgt->machine], lang, &proj_args)) {
			obj_array_extend(wk, args, proj_args);
		}
	}

	return args;
}

void
ca_setup_compiler_args_includes(struct workspace *wk, obj compiler, obj include_dirs, obj args, bool relativize)
{
	struct obj_compiler *comp = get_obj_compiler(wk, compiler);

	obj v;
	obj_array_for(wk, include_dirs, v) {
		const char *dir;
		enum {
			inc_type_normal,
			inc_type_system,
			inc_type_dirafter,
		} inc_type = inc_type_normal;
		{
			enum obj_type t = get_obj_type(wk, v);
			switch (t) {
			case obj_include_directory: {
				struct obj_include_directory *inc = get_obj_include_directory(wk, v);
				dir = get_cstr(wk, inc->path);
				if (inc->is_system) {
					inc_type = inc_type_system;
				} else if (inc->is_idirafter) {
					inc_type = inc_type_dirafter;
				}
				break;
			}
			case obj_string:
				dir = get_cstr(wk, v);
				break;
			default: LOG_E("invalid type for include directory '%s'", obj_type_to_s(t)); UNREACHABLE;
			}
		}

		TSTR(rel);
		if (relativize) {
			if (!fs_dir_exists(dir)) {
				continue;
			}

			if (path_is_absolute(dir)) {
				path_relative_to(wk, &rel, wk->build_root, dir);
				dir = rel.buf;
			}
		}

		switch (inc_type) {
		case inc_type_normal:
			push_args(wk, args, toolchain_compiler_include(wk, comp, dir));
			break;
		case inc_type_system:
			push_args(wk, args, toolchain_compiler_include_system(wk, comp, dir));
			break;
		case inc_type_dirafter:
			push_args(wk, args, toolchain_compiler_include_dirafter(wk, comp, dir));
			break;
		}
	};
}

static bool
ca_prepare_target_args(struct workspace *wk, const struct project *proj, struct obj_build_target *tgt)
{
	assert(!tgt->processed_args);

	tgt->processed_args = make_obj(wk, obj_dict);

	if (tgt->flags & build_tgt_generated_include) {
		obj inc;
		inc = make_obj(wk, obj_array);
		obj_array_push(wk, inc, tgt->private_path);
		obj_array_extend_nodup(wk, inc, tgt->dep_internal.include_directories);
		tgt->dep_internal.include_directories = inc;
	}

	obj _lang, _n;
	obj_dict_for(wk, tgt->required_compilers, _lang, _n) {
		(void)_n;
		enum compiler_language lang = _lang;
		obj comp_id;
		if (!obj_dict_geti(wk, proj->toolchains[tgt->machine], lang, &comp_id)) {
			LOG_E("No %s compiler defined for language %s",
				machine_kind_to_s(tgt->machine),
				compiler_language_to_s(lang));
			return false;
		}

		struct obj_compiler *comp = get_obj_compiler(wk, comp_id);

		obj args = ca_get_base_compiler_args(wk, proj, tgt, lang, comp_id);

		if (tgt->flags & build_tgt_flag_pic) {
			push_args(wk, args, toolchain_compiler_pic(wk, comp));
		}

		if (tgt->flags & build_tgt_flag_pie) {
			push_args(wk, args, toolchain_compiler_pie(wk, comp));
		}

		if (tgt->flags & build_tgt_flag_visibility) {
			push_args(wk, args, toolchain_compiler_visibility(wk, comp, tgt->visibility));
		}

		obj dedupd;
		obj_array_dedup(wk, tgt->dep_internal.include_directories, &dedupd);
		tgt->dep_internal.include_directories = dedupd;

		{ /* project includes */
			obj proj_incs;
			if (obj_dict_geti(wk, proj->include_dirs[tgt->machine], lang, &proj_incs)) {
				obj_array_extend(wk, tgt->dep_internal.include_directories, proj_incs);
				obj_array_dedup(wk, tgt->dep_internal.include_directories, &dedupd);
				tgt->dep_internal.include_directories = dedupd;
			}
		}

		ca_setup_compiler_args_includes(wk, comp_id, tgt->dep_internal.include_directories, args, true);

		obj args_post;
		args_post = make_obj(wk, obj_array);

		{ /* compile args */
			if (tgt->dep_internal.compile_args) {
				obj tgt_args;
				if (obj_dict_geti(wk, tgt->args, lang, &tgt_args)) {
					obj_array_extend(wk, tgt_args, tgt->dep_internal.compile_args);
				} else {
					obj_dict_seti(wk, tgt->args, lang, tgt->dep_internal.compile_args);
				}
			}
		}

		{ /* target args */
			obj tgt_args;
			if (obj_dict_geti(wk, tgt->args, lang, &tgt_args) && get_obj_array(wk, tgt_args)->len) {
				obj_array_extend(wk, args_post, tgt_args);
			}
		}

		if (tgt->pch) {
			obj pch;
			if (obj_dict_geti(wk, tgt->pch, lang, &pch)) {
				if (get_obj_type(wk, pch) != obj_build_target) {
					if (!tgt->processed_args_pch) {
						tgt->processed_args_pch = make_obj(wk, obj_dict);
					}

					obj args_dup;
					obj_array_dup(wk, args, &args_dup);
					obj_array_extend(wk, args_dup, args_post);
					push_args(wk, args_dup, toolchain_compiler_emit_pch(wk, comp));
					push_args(wk,
						args_dup,
						toolchain_compiler_force_language(
							wk, comp, compiler_language_to_hdr(lang)));
					obj_dict_seti(wk, tgt->processed_args_pch, lang, args_dup);
				}

				TSTR(dest_path);
				if (!tgt_src_to_pch_path(wk, tgt, lang, pch, &dest_path)) {
					return false;
				}

				push_args(wk, args, toolchain_compiler_include_pch(wk, comp, dest_path.buf));
			}
		}
		obj_array_extend(wk, args, args_post);

		obj_dict_seti(wk, tgt->processed_args, lang, args);
	}

	return true;
}

obj
ca_build_target_joined_args(struct workspace *wk, obj processed_args)
{
	obj joined;
	joined = make_obj(wk, obj_dict);

	obj lang, v;
	obj_dict_for(wk, processed_args, lang, v) {
		obj_dict_seti(wk, joined, lang, join_args_shell_ninja(wk, v));
	}

	return joined;
}

/*
 * linker args
 */

void
ca_get_option_link_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args_id)
{
	obj args;
	switch (comp->lang) {
#define TOOLCHAIN_ENUM(lang) \
	case compiler_language_##lang: ca_get_option_value_for_tgt(wk, proj, tgt, #lang "_link_args", &args); break;
		FOREACH_COMPILER_EXPOSED_LANGUAGE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
	default: return;
	}

	obj_array_extend(wk, args_id, args);
}

static void
ca_push_linker_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct obj_build_target *tgt,
	const struct args *args)
{
	if (!args->len) {
		return;
	}

	if (toolchain_compiler_do_linker_passthrough(wk, comp)) {
		args = toolchain_compiler_linker_passthrough(wk, comp, args);
	}

	push_args(wk, tgt->dep_internal.link_args, args);
}

static bool
ca_setup_optional_b_args_linker(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	const struct obj_build_target *tgt,
	obj args)
{
	obj opt;
	ca_get_option_value_for_tgt(wk, proj, tgt, "b_pgo", &opt);
	if (!str_eql(get_str(wk, opt), &STR("off"))) {
		uint32_t stage;
		const struct str *sl = get_str(wk, opt);
		if (str_eql(sl, &STR("generate"))) {
			stage = 0;
		} else if (str_eql(sl, &STR("use"))) {
			stage = 1;
		} else {
			UNREACHABLE;
			return false;
		}
		push_args(wk, args, toolchain_linker_pgo(wk, comp, stage));
	}

	ca_get_option_value_for_tgt(wk, proj, tgt, "b_sanitize", &opt);
	if (strcmp(get_cstr(wk, opt), "none") != 0) {
		push_args(wk, args, toolchain_linker_sanitize(wk, comp, get_cstr(wk, opt)));
	}

	ca_get_option_value_for_tgt(wk, proj, tgt, "b_lto", &opt);
	if (get_obj_bool(wk, opt)) {
		push_args(wk, args, toolchain_linker_enable_lto(wk, comp));
	}

	ca_get_option_value_for_tgt(wk, proj, tgt, "b_coverage", &opt);
	if (get_obj_bool(wk, opt)) {
		push_args(wk, args, toolchain_linker_coverage(wk, comp));
	}

	return true;
}

bool
ca_prepare_target_linker_args(struct workspace *wk,
	struct obj_compiler *comp,
	const struct project *proj,
	struct obj_build_target *tgt,
	bool relativize)
{
	if (!comp) {
		obj comp_id;
		if (!obj_dict_geti(wk, proj->toolchains[tgt->machine], tgt->dep_internal.link_language, &comp_id)) {
			LOG_E("no compiler defined for link language %s",
				compiler_language_to_s(tgt->dep_internal.link_language));
			return false;
		}

		comp = get_obj_compiler(wk, comp_id);
	}

	if (relativize) {
		ca_relativize_paths(wk, tgt->dep_internal.link_with, true, &tgt->dep_internal.link_with);
		ca_relativize_paths(wk, tgt->dep_internal.link_whole, true, &tgt->dep_internal.link_whole);
	}

	obj_array_dedup_in_place(wk, &tgt->dep_internal.link_with);
	obj_array_dedup_in_place(wk, &tgt->dep_internal.link_whole);
	obj_array_dedup_in_place(wk, &tgt->dep_internal.link_with_not_found);

	if (tgt->type & tgt_static_library) {
		// static libs don't need these args
		return true;
	}

	{
		struct ca_buildtype buildtype;
		ca_get_buildtype(wk, proj, tgt, &buildtype);

		if (buildtype.debug) {
			ca_push_linker_args(wk, comp, tgt, toolchain_linker_debug(wk, comp));
		}
	}

	ca_push_linker_args(wk, comp, tgt, toolchain_linker_always(wk, comp));
	ca_push_linker_args(wk, comp, tgt, toolchain_linker_as_needed(wk, comp));

	if (proj) {
		if (!(tgt->type & tgt_shared_module)) {
			ca_push_linker_args(wk, comp, tgt, toolchain_linker_no_undefined(wk, comp));
		}

		if (tgt->flags & build_tgt_flag_export_dynamic) {
			ca_push_linker_args(wk, comp, tgt, toolchain_linker_export_dynamic(wk, comp));
		}

		ca_setup_optional_b_args_linker(wk, comp, proj, tgt, tgt->dep_internal.link_args);

		{ /* option args (from option('x_link_args')) */
			ca_get_option_link_args(wk, comp, proj, tgt, tgt->dep_internal.link_args);
		}

		/* global args */
		obj global_args;
		if (obj_dict_geti(wk, wk->global_link_args[tgt->machine], comp->lang, &global_args)) {
			obj_array_extend(wk, tgt->dep_internal.link_args, global_args);
		}

		/* project args */
		obj proj_args;
		if (obj_dict_geti(wk, proj->link_args[tgt->machine], comp->lang, &proj_args)) {
			obj_array_extend(wk, tgt->dep_internal.link_args, proj_args);
		}

		if (tgt && tgt->flags & build_tgt_flag_pic) {
			push_args(wk, tgt->dep_internal.link_args, toolchain_compiler_pic(wk, comp));
		}
	}

	obj v;
	obj_array_for(wk, tgt->dep_internal.rpath, v) {
		if (!get_str(wk, v)->len) {
			continue;
		}

		ca_push_linker_args(wk, comp, tgt, toolchain_linker_rpath(wk, comp, get_cstr(wk, v)));
	}

	if (tgt->dep_internal.frameworks) {
		obj v;
		obj_array_for(wk, tgt->dep_internal.frameworks, v) {
			obj_array_push(wk, tgt->dep_internal.link_args, make_str(wk, "-framework"));
			obj_array_push(wk, tgt->dep_internal.link_args, v);
		}
	}

	bool have_link_whole = get_obj_array(wk, tgt->dep_internal.link_whole)->len,
	     have_link_with = have_link_whole || get_obj_array(wk, tgt->dep_internal.link_with)->len
			      || get_obj_array(wk, tgt->dep_internal.link_with_not_found)->len;

	if (have_link_with) {
		ca_push_linker_args(wk, comp, tgt, toolchain_linker_start_group(wk, comp));

		if (have_link_whole) {
			obj v;
			obj_array_for(wk, tgt->dep_internal.link_whole, v) {
				ca_push_linker_args(
					wk, comp, tgt, toolchain_linker_whole_archive(wk, comp, get_cstr(wk, v)));
			}
		}

		if (proj) {
			/* project link_with */
			obj proj_link_with;
			if (obj_dict_geti(wk, proj->link_with[tgt->machine], comp->lang, &proj_link_with)) {
				obj_array_extend(wk, tgt->dep_internal.link_args, proj_link_with);
			}
		}

		obj_array_extend(wk, tgt->dep_internal.link_args, tgt->dep_internal.link_with);

		obj v;
		obj_array_for(wk, tgt->dep_internal.link_with_not_found, v) {
			ca_push_linker_args(wk, comp, tgt, toolchain_linker_lib(wk, comp, get_cstr(wk, v)));
		};

		ca_push_linker_args(wk, comp, tgt, toolchain_linker_end_group(wk, comp));
	}

	if (tgt && (tgt->type & (tgt_dynamic_library | tgt_shared_module))) {
		ca_push_linker_args(wk, comp, tgt, toolchain_linker_soname(wk, comp, get_cstr(wk, tgt->soname)));
		if (tgt->type == tgt_shared_module) {
			ca_push_linker_args(wk, comp, tgt, toolchain_linker_allow_shlib_undefined(wk, comp));
			ca_push_linker_args(wk, comp, tgt, toolchain_linker_shared_module(wk, comp));
		} else {
			ca_push_linker_args(wk, comp, tgt, toolchain_linker_shared(wk, comp));

			if (tgt->implib) {
				obj rel;
				ca_relativize_path(wk, tgt->implib, true, &rel);
				ca_push_linker_args(
					wk, comp, tgt, toolchain_linker_implib(wk, comp, get_cstr(wk, rel)));
			}
		}
	}

	return true;
}

bool
ca_prepare_all_targets(struct workspace *wk)
{
	obj_array_push(wk, wk->backend_output_stack, make_str(wk, "preparing targets"));

	obj t;
	uint32_t proj_i;
	const struct project *proj;
	struct obj_build_target *tgt;

	for (proj_i = 0; proj_i < wk->projects.len; ++proj_i) {
		proj = arr_get(&wk->projects, proj_i);
		obj_array_push(wk, wk->backend_output_stack, proj->cfg.name);
		obj_array_for(wk, proj->targets, t) {
			switch (get_obj_type(wk, t)) {
			case obj_both_libs:
				t = decay_both_libs(wk, t);
				// fallthrough
			case obj_build_target: tgt = get_obj_build_target(wk, t); break;
			default: continue;
			}

			obj_array_push(wk, wk->backend_output_stack, tgt->name);
			if (!ca_prepare_target_args(wk, proj, tgt)) {
				return false;
			}
			if (!ca_prepare_target_linker_args(wk, 0, proj, tgt, true)) {
				return false;
			}
			obj_array_pop(wk, wk->backend_output_stack);
		}

		obj_array_pop(wk, wk->backend_output_stack);
	}

	obj_array_pop(wk, wk->backend_output_stack);
	return true;
}

/* */

struct ca_relativize_paths_ctx {
	bool relativize_strings;
	obj *oneshot;
	obj dest;
};

static enum iteration_result
ca_relativize_paths_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct ca_relativize_paths_ctx *ctx = _ctx;

	const char *str;

	if (get_obj_type(wk, val) == obj_string) {
		if (ctx->relativize_strings) {
			str = get_cstr(wk, val);
		} else {
			if (ctx->oneshot) {
				*ctx->oneshot = val;
			} else {
				obj_array_push(wk, ctx->dest, val);
			}
			return ir_cont;
		}
	} else {
		str = get_file_path(wk, val);
	}

	TSTR(buf);
	path_relative_to(wk, &buf, wk->build_root, str);
	obj s = tstr_into_str(wk, &buf);

	if (ctx->oneshot) {
		*ctx->oneshot = s;
	} else {
		obj_array_push(wk, ctx->dest, s);
	}
	return ir_cont;
}

void
ca_relativize_paths(struct workspace *wk, obj arr, bool relativize_strings, obj *res)
{
	*res = make_obj(wk, obj_array);
	struct ca_relativize_paths_ctx ctx = {
		.relativize_strings = relativize_strings,
		.dest = *res,
	};

	obj_array_foreach(wk, arr, &ctx, ca_relativize_paths_iter);
}

void
ca_relativize_path(struct workspace *wk, obj path, bool relativize_strings, obj *res)
{
	*res = make_obj(wk, obj_array);
	struct ca_relativize_paths_ctx ctx = {
		.relativize_strings = relativize_strings,
		.oneshot = res,
	};

	ca_relativize_paths_iter(wk, &ctx, path);
}

void
ca_relativize_path_push(struct workspace *wk, obj path, obj arr)
{
	struct ca_relativize_paths_ctx ctx = {
		.dest = arr,
	};

	ca_relativize_paths_iter(wk, &ctx, path);
}

obj
ca_regenerate_build_command(struct workspace *wk, bool opts_only)
{
	obj regen_args;
	regen_args = make_obj(wk, obj_array);

	if (!opts_only) {
		obj_array_push(wk, regen_args, make_str(wk, wk->argv0));
		obj_array_push(wk, regen_args, make_str(wk, "-C"));
		obj_array_push(wk, regen_args, make_str(wk, wk->source_root));
		obj_array_push(wk, regen_args, make_str(wk, "setup"));
	}

	obj key, val;
	obj_dict_for(wk, wk->global_opts, key, val) {
		(void)key;
		struct obj_option *o = get_obj_option(wk, val);
		if (o->source != option_value_source_environment) {
			continue;
		}

		// NOTE: This only handles options of type str or [str], which is okay since
		// the only options that can be set from the environment are of this
		// type.
		// TODO: The current implementation of array stringification would
		// choke on spaces, etc.

		const char *sval;
		switch (get_obj_type(wk, o->val)) {
		case obj_string: sval = get_cstr(wk, o->val); break;
		case obj_array: {
			obj joined;
			obj_array_join(wk, true, o->val, make_str(wk, ","), &joined);
			sval = get_cstr(wk, joined);
			break;
		}
		default: UNREACHABLE;
		}

		obj_array_push(wk, regen_args, make_strf(wk, "-D%s=%s", get_cstr(wk, o->name), sval));
	}

	uint32_t i;
	for (i = 0; i < wk->original_commandline.argc; ++i) {
		obj_array_push(wk, regen_args, make_str(wk, wk->original_commandline.argv[i]));
	}

	return regen_args;
}

obj
ca_backend_tgt_name(struct workspace *wk, obj tgt_id)
{
	switch (get_obj_type(wk, tgt_id)) {
	case obj_alias_target: return get_obj_alias_target(wk, tgt_id)->name; break;
	case obj_both_libs: tgt_id = decay_both_libs(wk, tgt_id);
	/* fallthrough */
	case obj_build_target: return get_obj_build_target(wk, tgt_id)->build_name; break;
	case obj_custom_target: return get_obj_custom_target(wk, tgt_id)->name; break;
	default: UNREACHABLE_RETURN;
	}
}
