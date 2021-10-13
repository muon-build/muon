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
	uint32_t err_node;
	obj arr, input, output, depfile, depends;
	bool skip_depends;
};

static bool
prefix_plus_index(const struct str *ss, const char *prefix, int64_t *index)
{
	uint32_t len = strlen(prefix);
	if (wk_str_startswith(ss, &WKSTR(prefix))) {
		return wk_str_to_i(&(struct str) {
			.s = &ss->s[len],
			.len = ss->len - len
		}, index);
	}

	return false;
}

static enum format_cb_result
format_cmd_arg_cb(struct workspace *wk, uint32_t node, void *_ctx, const struct str *strkey, uint32_t *elem)
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
		if (wk_cstreql(strkey, key_names[key])) {
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
		LOG_E("TODO: handle @%s@", strkey->s);
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
custom_target_cmd_fmt_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct custom_target_cmd_fmt_ctx *ctx = _ctx;

	obj ss;
	struct obj *o = get_obj(wk, val);

	switch (o->type) {
	case obj_build_target:
	case obj_external_program:
	case obj_file:
		if (!coerce_executable(wk, ctx->err_node, val, &ss)) {
			return ir_err;
		}

		if (!ctx->skip_depends) {
			obj_array_push(wk, ctx->depends, ss);
		}
		break;
	case obj_string: {
		if (strcmp(get_cstr(wk, val), "@INPUT@") == 0) {
			ctx->skip_depends = true;
			if (!obj_array_foreach(wk, ctx->input, ctx, custom_target_cmd_fmt_iter)) {
				return ir_err;
			}
			ctx->skip_depends = false;
			return ir_cont;
		} else if (strcmp(get_cstr(wk, val), "@OUTPUT@") == 0) {
			ctx->skip_depends = true;
			if (!obj_array_foreach(wk, ctx->output, ctx, custom_target_cmd_fmt_iter)) {
				return ir_err;
			}
			ctx->skip_depends = false;
			return ir_cont;
		}

		str s;
		if (!string_format(wk, ctx->err_node, get_obj(wk, val)->dat.str,
			&s, ctx, format_cmd_arg_cb)) {
			return ir_err;
		}
		make_obj(wk, &ss, obj_string)->dat.str = s;
		break;
	}
	default:
		interp_error(wk, ctx->err_node, "unable to coerce %o to string", val);
		return ir_err;
	}

	assert(get_obj(wk, ss)->type == obj_string);

	obj_array_push(wk, ctx->arr, ss);
	return ir_cont;
}

bool
process_custom_target_commandline(struct workspace *wk, uint32_t err_node,
	obj arr, obj input, obj output, obj depfile, obj depends, obj *res)
{
	make_obj(wk, res, obj_array);
	struct custom_target_cmd_fmt_ctx ctx = {
		.arr = *res,
		.err_node = err_node,
		.input = input,
		.output = output,
		.depfile = depfile,
		.depends = depends,
	};

	if (!obj_array_foreach_flat(wk, arr, &ctx, custom_target_cmd_fmt_iter)) {
		return false;
	}

	if (!get_obj(wk, *res)->dat.arr.len) {
		interp_error(wk, err_node, "cmd cannot be empty");
		return false;
	}

	return true;
}

static enum format_cb_result
format_cmd_output_cb(struct workspace *wk, uint32_t node, void *_ctx, const struct str *strkey, obj *elem)
{
	struct custom_target_cmd_fmt_ctx *ctx = _ctx;

	enum cmd_output_fmt_key {
		key_plainname,
		key_basename,
		cmd_output_fmt_key_count
	};

	const char *key_names[cmd_output_fmt_key_count] = {
		[key_plainname         ] = "PLAINNAME",
		[key_basename          ] = "BASENAME",
	};

	enum cmd_output_fmt_key key;
	for (key = 0; key < cmd_output_fmt_key_count; ++key) {
		if (wk_cstreql(strkey, key_names[key])) {
			break;
		}
	}

	if (key >= cmd_output_fmt_key_count) {
		return format_cb_not_found;
	}

	struct obj *in = get_obj(wk, ctx->input);
	if (in->dat.arr.len != 1) {
		interp_error(wk, ctx->err_node,
			"to use @PLAINNAME@ and @BASENAME@ in a custom "
			"target output, there must be exactly one input");
		return format_cb_error;
	}

	obj in0;
	obj_array_index(wk, ctx->input, 0, &in0);
	const struct str *ss = get_str(wk, get_obj(wk, in0)->dat.file);
	char buf[PATH_MAX];

	switch (key) {
	case key_plainname:
		if (!path_basename(buf, PATH_MAX, ss->s)) {
			return format_cb_error;
		}
		break;
	case key_basename: {
		char basename[PATH_MAX];
		if (!path_basename(basename, PATH_MAX, ss->s)) {
			return format_cb_error;
		} else if (!path_without_ext(buf, PATH_MAX, basename)) {
			return format_cb_error;
		}
		break;
	}
	default:
		assert(false && "unreachable");
		return format_cb_error;
	}

	*elem = make_str(wk, buf);
	return format_cb_found;
}

static enum iteration_result
custom_command_output_format_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct custom_target_cmd_fmt_ctx *ctx = _ctx;

	str s;
	if (!string_format(wk, ctx->err_node, get_obj(wk, v)->dat.str,
		&s, ctx, format_cmd_output_cb)) {
		return ir_err;
	}
	obj f;
	make_obj(wk, &f, obj_file)->dat.str = s;

	obj_array_push(wk, ctx->output, f);

	return ir_cont;
}

bool
func_custom_target(struct workspace *wk, obj _, uint32_t args_node, obj *res)
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

	obj input, raw_output, output, args;
	enum custom_target_flags flags = 0;

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

	if (!coerce_output_files(wk, akw[kw_output].node, akw[kw_output].val, &raw_output)) {
		return false;
	} else if (!get_obj(wk, raw_output)->dat.arr.len) {
		interp_error(wk, akw[kw_output].node, "output cannot be empty");
	}

	make_obj(wk, &output, obj_array);
	struct custom_target_cmd_fmt_ctx ctx = {
		.err_node = akw[kw_output].node,
		.input = input,
		.output = output,
	};
	if (!obj_array_foreach(wk, raw_output, &ctx, custom_command_output_format_iter)) {
		return false;
	}

	obj depends;
	make_obj(wk, &depends, obj_array);
	if (!process_custom_target_commandline(wk, akw[kw_command].node,
		akw[kw_command].val, input, output, akw[kw_depfile].val, depends, &args)) {
		return false;
	}

	if (akw[kw_capture].set && get_obj(wk, akw[kw_capture].val)->dat.boolean) {
		flags |= custom_target_capture;
	}

	struct obj *tgt = make_obj(wk, res, obj_custom_target);
	tgt->dat.custom_target.name = get_obj(wk, an[0].val)->dat.str;
	LOG_I("adding custom target '%s'", get_cstr(wk, tgt->dat.custom_target.name));
	tgt->dat.custom_target.args = args;
	tgt->dat.custom_target.input = input;
	tgt->dat.custom_target.output = output;
	tgt->dat.custom_target.depends = depends;
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

	obj_array_push(wk, current_project(wk)->targets, *res);
	return true;
}
