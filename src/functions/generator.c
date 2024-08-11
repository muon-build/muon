/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "coerce.h"
#include "error.h"
#include "functions/build_target.h"
#include "functions/generator.h"
#include "functions/kernel/custom_target.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/path.h"

bool
generated_list_process_file(struct workspace *wk,
	uint32_t node,
	struct obj_generator *g,
	struct obj_generated_list *gl,
	const char *dir,
	bool add_targets,
	obj val,
	obj *res,
	bool *generated_include)
{
	SBUF(path);
	const char *output_dir = dir;

	if (gl->preserve_path_from) {
		const char *src = get_file_path(wk, val), *base = get_cstr(wk, gl->preserve_path_from);
		assert(path_is_subpath(base, src));

		SBUF(dest_dir);

		path_relative_to(wk, &path, base, src);
		path_dirname(wk, &dest_dir, path.buf);
		path_join(wk, &path, dir, dest_dir.buf);
		output_dir = path.buf;
	}

	struct make_custom_target_opts opts = {
		.input_node = node,
		.output_node = node,
		.command_node = node,
		.input_orig = val,
		.output_orig = g->output,
		.output_dir = output_dir,
		.build_dir = dir,
		.command_orig = g->raw_command,
		.depfile_orig = g->depfile,
		.capture = g->capture,
		.feed = g->feed,
		.extra_args = gl->extra_arguments,
		.extra_args_valid = true,
	};

	obj tgt;
	if (!make_custom_target(wk, &opts, &tgt)) {
		return false;
	}

	struct obj_custom_target *t = get_obj_custom_target(wk, tgt);

	t->env = gl->env;

	obj name;
	if (add_targets) {
		name = make_str(wk, "");
	}

	{
		obj tmp_arr, file;
		make_obj(wk, &tmp_arr, obj_array);

		obj_array_for(wk, t->output, file) {
			obj_array_push(wk, tmp_arr, file);

			if (add_targets) {
				const char *generated_path = get_cstr(wk, *get_obj_file(wk, file));

				enum compiler_language l;
				if (!*generated_include && filename_to_compiler_language(generated_path, &l)
					&& languages[l].is_header) {
					*generated_include = true;
				}

				SBUF(rel);
				path_relative_to(wk, &rel, wk->build_root, generated_path);

				str_app(wk, &name, " ");
				str_app(wk, &name, rel.buf);
			}
		}

		obj_array_extend_nodup(wk, *res, tmp_arr);
	}

	if (add_targets) {
		t->name = make_strf(wk, "<gen:%s>", get_cstr(wk, name));
		if (g->depends) {
			obj_array_extend(wk, t->depends, g->depends);
		}
		obj_array_push(wk, current_project(wk)->targets, tgt);
	}

	return true;
}

bool
generated_list_process_for_target(struct workspace *wk,
	uint32_t node,
	obj generated_list,
	obj build_target,
	bool add_targets,
	obj *res)
{
	struct obj_generated_list *gl = get_obj_generated_list(wk, generated_list);
	struct obj_generator *g = get_obj_generator(wk, gl->generator);

	enum obj_type t = get_obj_type(wk, build_target);

	const char *dir;

	switch (t) {
	case obj_both_libs: build_target = get_obj_both_libs(wk, build_target)->dynamic_lib;
	/* fallthrough */
	case obj_build_target: dir = get_cstr(wk, get_obj_build_target(wk, build_target)->private_path); break;
	case obj_custom_target: {
		dir = get_cstr(wk, get_obj_custom_target(wk, build_target)->private_path);
		break;
	}
	default: UNREACHABLE;
	}

	make_obj(wk, res, obj_array);

	bool generated_include = false;

	obj val;
	obj_array_for(wk, gl->input, val) {
		if (get_obj_type(wk, val) == obj_generated_list) {
			obj sub_res;
			if (!generated_list_process_for_target(wk, node, val, build_target, add_targets, &sub_res)) {
				return false;
			}

			obj file;
			obj_array_for(wk, sub_res, file) {
				if (!generated_list_process_file(
					    wk, node, g, gl, dir, add_targets, file, res, &generated_include)) {
					return false;
				}
			}
			continue;
		}

		if (!generated_list_process_file(wk, node, g, gl, dir, add_targets, val, res, &generated_include)) {
			return false;
		}
	}

	if (add_targets && t == obj_build_target && generated_include) {
		get_obj_build_target(wk, build_target)->flags |= build_tgt_generated_include;
	}

	return true;
}

static bool
func_generator_process(struct workspace *wk, obj gen, obj *res)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | tc_coercible_files | tc_generated_list }, ARG_TYPE_NULL };
	enum kwargs {
		kw_extra_args,
		kw_preserve_path_from,
		kw_env,
	};
	struct args_kw akw[] = {
		[kw_extra_args] = { "extra_args", TYPE_TAG_LISTIFY | obj_string },
		[kw_preserve_path_from] = { "preserve_path_from", obj_string },
		[kw_env] = { "env", tc_coercible_env },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	make_obj(wk, res, obj_generated_list);
	struct obj_generated_list *gl = get_obj_generated_list(wk, *res);
	gl->generator = gen;
	gl->extra_arguments = akw[kw_extra_args].val;
	gl->preserve_path_from = akw[kw_preserve_path_from].val;

	if (!coerce_environment_from_kwarg(wk, &akw[kw_env], true, &gl->env)) {
		return false;
	}

	make_obj(wk, &gl->input, obj_array);
	{
		obj v, coercible_files;
		make_obj(wk, &coercible_files, obj_array);

		obj_array_for(wk, an[0].val, v) {
			obj_array_push(wk, get_obj_type(wk, v) == obj_generated_list ? gl->input : coercible_files, v);
		}

		obj files;
		if (!coerce_files(wk, an[0].node, coercible_files, &files)) {
			return false;
		}

		obj_array_extend_nodup(wk, gl->input, files);
	}

	if (gl->preserve_path_from) {
		if (!path_is_absolute(get_cstr(wk, gl->preserve_path_from))) {
			vm_error_at(wk, akw[kw_preserve_path_from].node, "preserve_path_from must be an absolute path");
			return false;
		}

		obj f;
		obj_array_for(wk, gl->input, f) {
			const char *src = get_file_path(wk, f), *base = get_cstr(wk, gl->preserve_path_from);

			if (!path_is_subpath(base, src)) {
				vm_error_at(wk,
					akw[kw_preserve_path_from].node,
					"source file '%s' is not a subdir of preserve_path_from path '%s'",
					src,
					base);
				return false;
			}
		}
	}

	return true;
}

const struct func_impl impl_tbl_generator[] = {
	{ "process", func_generator_process, tc_generated_list },
	{ NULL, NULL },
};
