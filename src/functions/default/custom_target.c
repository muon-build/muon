#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "coerce.h"
#include "functions/default/custom_target.h"
#include "functions/string.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

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
		*elem = make_str(wk, wk->source_root);
		return format_cb_found;
	case key_build_root:
		/* @BUILD_ROOT@: the path to the root of the build tree.
		 * Depending on the backend, this may be an absolute or a
		 * relative to current workdir path. */
		*elem = make_str(wk, wk->build_root);
		return format_cb_found;
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
make_custom_target(struct workspace *wk,
	obj name,
	uint32_t input_node,
	uint32_t output_node,
	uint32_t command_node,
	obj input_orig,
	obj output_orig,
	obj command_orig,
	obj depfile_orig,
	bool capture,
	obj *res)
{
	obj input, raw_output, output, args;
	enum custom_target_flags flags = 0;

	if (input_orig) {
		if (!coerce_files(wk, input_node, input_orig, &input)) {
			return false;
		} else if (!get_obj(wk, input)->dat.arr.len) {
			interp_error(wk, input_node, "input cannot be empty");
		}
	} else {
		make_obj(wk, &input, obj_array);
	}

	if (!coerce_output_files(wk, output_node, output_orig, &raw_output)) {
		return false;
	} else if (!get_obj(wk, raw_output)->dat.arr.len) {
		interp_error(wk, output_node, "output cannot be empty");
	}

	make_obj(wk, &output, obj_array);
	struct custom_target_cmd_fmt_ctx ctx = {
		.err_node = output_node,
		.input = input,
		.output = output,
	};
	if (!obj_array_foreach(wk, raw_output, &ctx, custom_command_output_format_iter)) {
		return false;
	}

	obj depends;
	make_obj(wk, &depends, obj_array);
	if (!process_custom_target_commandline(wk, command_node,
		command_orig, input, output, depfile_orig, depends, &args)) {
		return false;
	}

	if (capture) {
		flags |= custom_target_capture;
	}

	struct obj *tgt = make_obj(wk, res, obj_custom_target);
	tgt->dat.custom_target.name = get_obj(wk, name)->dat.str;
	tgt->dat.custom_target.args = args;
	tgt->dat.custom_target.input = input;
	tgt->dat.custom_target.output = output;
	tgt->dat.custom_target.depends = depends;
	tgt->dat.custom_target.flags = flags;

	return true;
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
		kw_depend_files,
	};
	struct args_kw akw[] = {
		[kw_input] = { "input", obj_any, },
		[kw_output] = { "output", obj_any, .required = true },
		[kw_command] = { "command", obj_array, .required = true },
		[kw_capture] = { "capture", obj_bool },
		[kw_install] = { "install", obj_bool },
		[kw_install_dir] = { "install_dir", obj_any }, // TODO
		[kw_install_mode] = { "install_mode", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_build_by_default] = { "build_by_default", obj_bool },
		[kw_depfile] = { "depfile", obj_string },
		[kw_depend_files] = { "depend_files", ARG_TYPE_ARRAY_OF | obj_any },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (!make_custom_target(
		wk,
		an[0].val,
		akw[kw_input].node,
		akw[kw_output].node,
		akw[kw_command].node,
		akw[kw_input].val,
		akw[kw_output].val,
		akw[kw_command].val,
		akw[kw_depfile].val,
		akw[kw_capture].set ? get_obj(wk, akw[kw_capture].val)->dat.boolean : false,
		res
		)) {
		return false;
	}

	struct obj *tgt = get_obj(wk, *res);

	if (akw[kw_depend_files].set) {
		obj depend_files;
		if (!coerce_string_array(wk, akw[kw_depend_files].node, akw[kw_depend_files].val, &depend_files)) {
			return false;
		}

		obj_array_extend(wk, tgt->dat.custom_target.depends, depend_files);
	}

	LOG_I("adding custom target '%s'", get_cstr(wk, tgt->dat.custom_target.name));

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

		if (!push_install_targets(wk, tgt->dat.custom_target.output, akw[kw_install_dir].val, install_mode_id)) {
			return false;
		}
	}

	obj_array_push(wk, current_project(wk)->targets, *res);
	return true;
}

bool
func_vcs_tag(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	enum kwargs {
		kw_input,
		kw_output,
		kw_command,
		kw_fallback,
		kw_replace_string,
	};
	struct args_kw akw[] = {
		[kw_input]       = { "input", obj_string, .required = true },
		[kw_output]      = { "output", obj_string, .required = true },
		[kw_command]     = { "command", obj_array },
		[kw_fallback]     = { "fallback", obj_string },
		[kw_replace_string] = { "replace_string", obj_string },
		0
	};

	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	obj replace_string = akw[kw_replace_string].set
		? akw[kw_replace_string].val
		: make_str(wk, "@@VCS_TAG@@");

	obj fallback;
	if (akw[kw_fallback].set) {
		fallback = akw[kw_fallback].val;
	} else {
		make_obj(wk, &fallback, obj_string)->dat.str = current_project(wk)->cfg.version;
	}

	obj command;
	make_obj(wk, &command, obj_array);

	push_args_null_terminated(wk, command, (char *const []){
		wk->argv0,
		"-C",
		NULL,
	});

	obj cwd;
	make_obj(wk, &cwd, obj_string)->dat.str = current_project(wk)->build_dir;
	obj_array_push(wk, command, cwd);

	push_args_null_terminated(wk, command, (char *const []){
		"internal",
		"eval",
		"-e",
		"vcs_tagger.meson",
		NULL,
	});

	obj input;
	if (!coerce_string_to_file(wk, akw[kw_input].val, &input)) {
		return false;
	}

	obj_array_push(wk, command, input);
	obj_array_push(wk, command, akw[kw_output].val);
	obj_array_push(wk, command, replace_string);
	obj_array_push(wk, command, fallback);

	if (akw[kw_command].set) {
		obj cmd;
		obj_array_dup(wk, akw[kw_command].val, &cmd);
		obj_array_extend(wk, command, cmd);
	}

	if (!make_custom_target(
		wk,
		make_str(wk, "vcs_tag"),
		akw[kw_input].node,
		akw[kw_output].node,
		args_node,
		akw[kw_input].val,
		akw[kw_output].val,
		command,
		0,
		false,
		res
		)) {
		return false;
	}

	obj_array_push(wk, current_project(wk)->targets, *res);
	return true;
}
