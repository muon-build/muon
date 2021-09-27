#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "coerce.h"
#include "functions/default/custom_target.h"
#include "functions/string.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/path.h"
#include "platform/filesystem.h"

struct custom_target_cmd_fmt_ctx {
	uint32_t arr, err_node;
	uint32_t input, output, depfile;
};

static bool
prefix_plus_index(const char *str, const char *prefix, int64_t *index)
{
	uint32_t len = strlen(prefix);
	if (strlen(str) > len && strncmp(prefix, str, len) == 0) {
		char *endptr;
		*index = strtol(&str[len], &endptr, 10);

		if (*endptr) {
			return false;
		}
		return true;
	}

	return false;
}

static enum format_cb_result
format_cmd_arg_cb(struct workspace *wk, uint32_t node, void *_ctx, const char *strkey, uint32_t *elem)
{
	struct custom_target_cmd_fmt_ctx *ctx = _ctx;

	enum cmd_arg_fmt_key {
		key_input,
		key_output,
		key_outdir,
		key_depfile,
		key_plainname,
		key_basename,
		key_private_dir,
		key_source_root,
		key_build_root,
		key_current_source_dir,
		cmd_arg_fmt_key_count,
	};

	const char *key_names[cmd_arg_fmt_key_count] = {
		[key_input             ] = "INPUT",
		[key_output            ] = "OUTPUT",
		[key_outdir            ] = "OUTDIR",
		[key_depfile           ] = "DEPFILE",
		[key_plainname         ] = "PLAINNAME",
		[key_basename          ] = "BASENAME",
		[key_private_dir       ] = "PRIVATE_DIR",
		[key_source_root       ] = "SOURCE_ROOT",
		[key_build_root        ] = "BUILD_ROOT",
		[key_current_source_dir] = "CURRENT_SOURCE_DIR",
	};

	enum cmd_arg_fmt_key key;
	for (key = 0; key < cmd_arg_fmt_key_count; ++key) {
		if (strcmp(key_names[key], strkey) == 0) {
			break;
		}
	}

	obj e;

	switch (key) {
	case key_input:
	case key_output: {
		obj arr = key == key_input ? ctx->input : ctx->output;

		int64_t index = 0;
		if (!boundscheck(wk, ctx->err_node, arr, &index)) {
			return format_cb_error;
		}
		obj_array_index(wk, arr, 0, &e);

		make_obj(wk, elem, obj_string)->dat.str = get_obj(wk, e)->dat.file;
		return format_cb_found;
	}
	case key_outdir:
		/* @OUTDIR@: the full path to the directory where the output(s)
		 * must be written */
		make_obj(wk, elem, obj_string)->dat.str = current_project(wk)->build_dir;
		return format_cb_found;
	case key_current_source_dir:
		/* @CURRENT_SOURCE_DIR@: this is the directory where the
		 * currently processed meson.build is located in. Depending on
		 * the backend, this may be an absolute or a relative to
		 * current workdir path. */
		make_obj(wk, elem, obj_string)->dat.str = current_project(wk)->cwd;
		return format_cb_found;
	case key_private_dir:
		/* @PRIVATE_DIR@ (since 0.50.1): path to a directory where the
		 * custom target must store all its intermediate files. */
		make_obj(wk, elem, obj_string)->dat.str = wk_str_push(wk, "/tmp");
		return format_cb_found;
	case key_depfile:
		*elem = ctx->depfile;
		return format_cb_found;
	case key_source_root:
	/* @SOURCE_ROOT@: the path to the root of the source tree.
	 * Depending on the backend, this may be an absolute or a
	 * relative to current workdir path. */
	case key_build_root:
	/* @BUILD_ROOT@: the path to the root of the build tree.
	 * Depending on the backend, this may be an absolute or a
	 * relative to current workdir path. */
	case key_plainname:
	/* @PLAINNAME@: the input filename, without a path */
	case key_basename:
		/* @BASENAME@: the input filename, with extension removed */
		/* @DEPFILE@: the full path to the dependency file passed to
		 * depfile */
		LOG_E("TODO: handle @%s@", strkey);
		return format_cb_error;
	default:
		break;
	}


	int64_t index;
	uint32_t arr;

	if (prefix_plus_index(strkey, "INPUT", &index)) {
		arr = ctx->input;
	} else if (prefix_plus_index(strkey, "OUTPUT", &index)) {
		arr = ctx->output;
	} else {
		return format_cb_not_found;
	}

	if (!boundscheck(wk, ctx->err_node, arr, &index)) {
		return format_cb_error;
	} else if (!obj_array_index(wk, arr, index, &e)) {
		return format_cb_error;
	}

	make_obj(wk, elem, obj_string)->dat.str = get_obj(wk, e)->dat.file;
	return format_cb_found;
}

static enum iteration_result
custom_target_cmd_fmt_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct custom_target_cmd_fmt_ctx *ctx = _ctx;

	uint32_t str;
	struct obj *obj;

	switch ((obj = get_obj(wk, val))->type) {
	case obj_build_target:
	case obj_external_program:
	case obj_file:
		if (!coerce_executable(wk, ctx->err_node, val, &str)) {
			return ir_err;
		}
		break;
	case obj_string: {
		if (strcmp(wk_objstr(wk, val), "@INPUT@") == 0) {
			if (!obj_array_foreach(wk, ctx->input, ctx, custom_target_cmd_fmt_iter)) {
				return ir_err;
			}
			return ir_cont;
		} else if (strcmp(wk_objstr(wk, val), "@OUTPUT@") == 0) {
			if (!obj_array_foreach(wk, ctx->output, ctx, custom_target_cmd_fmt_iter)) {
				return ir_err;
			}
			return ir_cont;
		}

		uint32_t s;
		if (!string_format(wk, ctx->err_node, get_obj(wk, val)->dat.str,
			&s, ctx, format_cmd_arg_cb)) {
			return ir_err;
		}
		make_obj(wk, &str, obj_string)->dat.str = s;
		break;
	}
	default:
		interp_error(wk, ctx->err_node, "unable to coerce '%s' to string", obj_type_to_s(obj->type));
		return ir_err;
	}

	assert(get_obj(wk, str)->type == obj_string);

	obj_array_push(wk, ctx->arr, str);
	return ir_cont;
}

bool
process_custom_target_commandline(struct workspace *wk, uint32_t err_node,
	uint32_t arr, uint32_t input, uint32_t output, uint32_t depfile,
	uint32_t *res)
{
	make_obj(wk, res, obj_array);
	struct custom_target_cmd_fmt_ctx ctx = {
		.arr = *res,
		.err_node = err_node,
		.input = input,
		.output = output,
		.depfile = depfile,
	};

	if (!obj_array_foreach_flat(wk, arr, &ctx, custom_target_cmd_fmt_iter)) {
		return false;
	}

	if (!get_obj(wk, *res)->dat.arr.len) {
		interp_error(wk, err_node, "cmd cannot be empty");
		return false;
	}

	obj cmd;
	obj_array_index(wk, *res, 0, &cmd);

	/* TODO: this needs to be handle differentiation between commands that
	   are build targets */
	/* if (!path_is_absolute(wk_objstr(wk, cmd))) { */
	/* 	const char *cmd_path; */
	/* 	if (!fs_find_cmd(wk_objstr(wk, cmd), &cmd_path)) { */
	/* 		interp_error(wk, err_node, "command '%s' not found", */
	/* 			wk_objstr(wk, cmd)); */
	/* 		return false; */
	/* 	} */

	/* 	obj_array_set(wk, *res, 0, make_str(wk, cmd_path)); */
	/* } */

	return true;
}

bool
func_custom_target(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_input,
		kw_output,
		kw_command,
		kw_capture,
		kw_install,
		kw_install_dir,
		kw_install_mode,
		kw_build_by_default,
		kw_depfile,
	};
	struct args_kw akw[] = {
		[kw_input]       = { "input", obj_any, },
		[kw_output]      = { "output", obj_any, .required = true },
		[kw_command]     = { "command", obj_array, .required = true },
		[kw_capture]     = { "capture", obj_bool },
		[kw_install]     = { "install", obj_bool },
		[kw_install_dir] = { "install_dir", obj_any }, // TODO
		[kw_install_mode] = { "install_mode", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_build_by_default] = { "build_by_default", obj_bool },
		[kw_depfile]     = { "depfile", obj_string },
		0
	};
	uint32_t input, output, args, flags = 0;

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (akw[kw_input].set) {
		if (!coerce_files(wk, akw[kw_input].node, akw[kw_input].val, &input)) {
			return false;
		} else if (!get_obj(wk, input)->dat.arr.len) {
			interp_error(wk, akw[kw_input].node, "input cannot be empty");
		}
	} else {
		make_obj(wk, &input, obj_array);
	}

	if (!coerce_output_files(wk, akw[kw_output].node, akw[kw_output].val, &output)) {
		return false;
	} else if (!get_obj(wk, output)->dat.arr.len) {
		interp_error(wk, akw[kw_output].node, "output cannot be empty");
	}

	if (!process_custom_target_commandline(wk, akw[kw_command].node,
		akw[kw_command].val, input, output, akw[kw_depfile].val, &args)) {
		return false;
	}

	if (akw[kw_capture].set && get_obj(wk, akw[kw_capture].val)->dat.boolean) {
		flags |= custom_target_capture;
	}

	struct obj *tgt = make_obj(wk, obj, obj_custom_target);
	tgt->dat.custom_target.name = get_obj(wk, an[0].val)->dat.str;
	LOG_I("adding custom target '%s'", wk_str(wk, tgt->dat.custom_target.name));
	tgt->dat.custom_target.args = args;
	tgt->dat.custom_target.input = input;
	tgt->dat.custom_target.output = output;
	tgt->dat.custom_target.flags = flags;

	if ((akw[kw_install].set && get_obj(wk, akw[kw_install].val)->dat.boolean)
	    || (!akw[kw_install].set && akw[kw_install_dir].set)) {
		if (!akw[kw_install_dir].set) {
			interp_error(wk, akw[kw_install].node, "custom target installation requires install_dir");
			return false;
		}

		uint32_t install_mode_id = 0;
		if (akw[kw_install_mode].set) {
			install_mode_id = akw[kw_install_mode].val;
		}

		if (!push_install_targets(wk, 0, output, akw[kw_install_dir].val, install_mode_id)) {
			return false;
		}
	}

	obj_array_push(wk, current_project(wk)->targets, *obj);
	return true;
}
