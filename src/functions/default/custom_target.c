#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "coerce.h"
#include "functions/default/custom_target.h"
#include "functions/generator.h"
#include "functions/string.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

struct custom_target_cmd_fmt_ctx {
	uint32_t err_node;
	obj arr, input, output, depfile, depends, name;
	bool skip_depends;
	bool relativize;
};

static bool
prefix_plus_index(const struct str *ss, const char *prefix, int64_t *index)
{
	uint32_t len = strlen(prefix);
	if (str_startswith(ss, &WKSTR(prefix))) {
		return str_to_i(&(struct str) {
			.s = &ss->s[len],
			.len = ss->len - len
		}, index);
	}

	return false;
}

static bool
str_relative_to_build_root(struct workspace *wk,
	struct custom_target_cmd_fmt_ctx *ctx, const char *path_orig, obj *res)
{
	char rel[PATH_MAX]; //, abs[PATH_MAX];
	const char *path = path_orig;

	if (!ctx->relativize) {
		*res = make_str(wk, path);
		return true;
	}

	if (!path_is_absolute(path)) {
		*res = make_str(wk, path);
		return true;
	}

	if (!path_relative_to(rel, PATH_MAX, wk->build_root, path)) {
		return false;
	}

	*res = make_str(wk, rel);
	return true;
}

static enum format_cb_result
format_cmd_arg_cb(struct workspace *wk, uint32_t node, void *_ctx, const struct str *strkey, obj *elem)
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

	const struct {
		char *key;
		bool needs_name;
	} key_names[cmd_arg_fmt_key_count] = {
		[key_input             ] = { "INPUT", },
		[key_output            ] = { "OUTPUT", },
		[key_outdir            ] = { "OUTDIR", },
		[key_depfile           ] = { "DEPFILE", },
		[key_plainname         ] = { "PLAINNAME", },
		[key_basename          ] = { "BASENAME", },
		[key_private_dir       ] = { "PRIVATE_DIR", true, },
		[key_source_root       ] = { "SOURCE_ROOT", },
		[key_build_root        ] = { "BUILD_ROOT", },
		[key_current_source_dir] = { "CURRENT_SOURCE_DIR", },
	};

	enum cmd_arg_fmt_key key;
	for (key = 0; key < cmd_arg_fmt_key_count; ++key) {
		if (key_names[key].needs_name && !ctx->name) {
			continue;
		}

		if (str_eql(strkey, &WKSTR(key_names[key].key))) {
			break;
		}
	}

	obj e;

	switch (key) {
	case key_input:
	case key_output: {
		obj arr = key == key_input ? ctx->input : ctx->output;

		int64_t index = 0;
		if (!boundscheck(wk, ctx->err_node, get_obj_array(wk, arr)->len, &index)) {
			return format_cb_error;
		}
		obj_array_index(wk, arr, 0, &e);

		if (!str_relative_to_build_root(wk, ctx, get_file_path(wk, e), elem)) {
			return format_cb_error;
		}
		return format_cb_found;
	}
	case key_outdir:
		/* @OUTDIR@: the full path to the directory where the output(s)
		 * must be written */
		if (!str_relative_to_build_root(wk, ctx, get_cstr(wk, current_project(wk)->build_dir), elem)) {
			return format_cb_error;
		}
		return format_cb_found;
	case key_current_source_dir:
		/* @CURRENT_SOURCE_DIR@: this is the directory where the
		 * currently processed meson.build is located in. Depending on
		 * the backend, this may be an absolute or a relative to
		 * current workdir path. */
		if (!str_relative_to_build_root(wk, ctx, get_cstr(wk, current_project(wk)->cwd), elem)) {
			return format_cb_error;
		}
		return format_cb_found;
	case key_private_dir: {
		/* @PRIVATE_DIR@ (since 0.50.1): path to a directory where the
		 * custom target must store all its intermediate files. */
		char path[PATH_MAX];
		if (!path_join(path, PATH_MAX, get_cstr(wk, current_project(wk)->build_dir), get_cstr(wk, ctx->name))) {
			return format_cb_error;
		} else if (!path_add_suffix(path, PATH_MAX, ".p")) {
			return format_cb_error;
		}

		if (!str_relative_to_build_root(wk, ctx, path, elem)) {
			return format_cb_error;
		}
		return format_cb_found;
	}
	case key_depfile:
		/* @DEPFILE@: the full path to the dependency file passed to
		 * depfile */
		if (!str_relative_to_build_root(wk, ctx, get_cstr(wk, ctx->depfile), elem)) {
			return format_cb_error;
		}
		return format_cb_found;
	case key_source_root:
		/* @SOURCE_ROOT@: the path to the root of the source tree.
		 * Depending on the backend, this may be an absolute or a
		 * relative to current workdir path. */
		if (!str_relative_to_build_root(wk, ctx, wk->source_root, elem)) {
			return format_cb_error;
		}
		return format_cb_found;
	case key_build_root:
		/* @BUILD_ROOT@: the path to the root of the build tree.
		 * Depending on the backend, this may be an absolute or a
		 * relative to current workdir path. */
		if (!str_relative_to_build_root(wk, ctx, wk->build_root, elem)) {
			return format_cb_error;
		}
		return format_cb_found;
	case key_plainname:
	/* @PLAINNAME@: the input filename, without a path */
	case key_basename: {
		/* @BASENAME@: the input filename, with extension removed */
		struct obj_array *in = get_obj_array(wk, ctx->input);
		if (in->len != 1) {
			interp_error(wk, ctx->err_node,
				"to use @PLAINNAME@ and @BASENAME@ in a custom "
				"target command, there must be exactly one input");
			return format_cb_error;
		}

		obj in0;
		obj_array_index(wk, ctx->input, 0, &in0);
		const struct str *orig_str = get_str(wk, *get_obj_file(wk, in0));
		char plainname[PATH_MAX];

		if (!path_basename(plainname, PATH_MAX, orig_str->s)) {
			return format_cb_error;
		}

		if (key == key_basename) {
			char basename[PATH_MAX];
			if (!path_without_ext(basename, PATH_MAX, plainname)) {
				return format_cb_error;
			}

			if (!str_relative_to_build_root(wk, ctx, basename, elem)) {
				return format_cb_error;
			}
		} else {
			if (!str_relative_to_build_root(wk, ctx, plainname, elem)) {
				return format_cb_error;
			}
		}
		return format_cb_found;
		assert(false && "unreachable");
	}
	default:
		break;
	}


	int64_t index;
	obj arr;

	if (prefix_plus_index(strkey, "INPUT", &index)) {
		arr = ctx->input;
	} else if (prefix_plus_index(strkey, "OUTPUT", &index)) {
		arr = ctx->output;
	} else {
		return format_cb_not_found;
	}

	if (!boundscheck(wk, ctx->err_node, get_obj_array(wk, arr)->len, &index)) {
		return format_cb_error;
	}

	obj_array_index(wk, arr, index, &e);

	if (!str_relative_to_build_root(wk, ctx, get_file_path(wk, e), elem)) {
		return format_cb_error;
	}
	return format_cb_found;
}

static enum iteration_result
custom_target_cmd_fmt_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct custom_target_cmd_fmt_ctx *ctx = _ctx;

	obj ss;
	enum obj_type t = get_obj_type(wk, val);

	switch (t) {
	case obj_build_target:
	case obj_external_program:
	case obj_file: {
		obj str;
		if (!coerce_executable(wk, ctx->err_node, val, &str)) {
			return ir_err;
		}

		if (!str_relative_to_build_root(wk, ctx, get_cstr(wk, str), &ss)) {
			return false;
		}

		if (!ctx->skip_depends) {
			obj_array_push(wk, ctx->depends, ss);
		}
		break;
	}
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

		obj s;
		if (!string_format(wk, ctx->err_node, val, &s, ctx, format_cmd_arg_cb)) {
			return ir_err;
		}
		ss = s;
		break;
	}
	case obj_custom_target: {
		obj output = get_obj_custom_target(wk, val)->output;
		struct obj_array *out = get_obj_array(wk, output);
		if (out->len != 1) {
			interp_error(wk, ctx->err_node, "unable to coerce custom target with multiple outputs to string");
			return ir_err;
		}

		obj f;
		obj_array_index(wk, output, 0, &f);

		if (!str_relative_to_build_root(wk, ctx, get_file_path(wk, f), &ss)) {
			return false;
		}

		if (!ctx->skip_depends) {
			obj_array_push(wk, ctx->depends, ss);
		}
		break;
	}
	default:
		interp_error(wk, ctx->err_node, "unable to coerce %o to string", val);
		return ir_err;
	}

	assert(get_obj_type(wk, ss) == obj_string);

	obj_array_push(wk, ctx->arr, ss);
	return ir_cont;
}

bool
process_custom_target_commandline(struct workspace *wk, uint32_t err_node, bool relativize,
	obj name, obj arr, obj input, obj output, obj depfile, obj depends, obj *res)
{
	make_obj(wk, res, obj_array);
	struct custom_target_cmd_fmt_ctx ctx = {
		.arr = *res,
		.err_node = err_node,
		.input = input,
		.output = output,
		.depfile = depfile,
		.depends = depends,
		.name = name,
		.relativize = relativize
	};

	if (!obj_array_foreach_flat(wk, arr, &ctx, custom_target_cmd_fmt_iter)) {
		return false;
	}

	if (!get_obj_array(wk, *res)->len) {
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
		[key_plainname] = "PLAINNAME",
		[key_basename] = "BASENAME",
	};

	enum cmd_output_fmt_key key;
	for (key = 0; key < cmd_output_fmt_key_count; ++key) {
		if (str_eql(strkey, &WKSTR(key_names[key]))) {
			break;
		}
	}

	if (key >= cmd_output_fmt_key_count) {
		return format_cb_not_found;
	}

	struct obj_array *in = get_obj_array(wk, ctx->input);
	if (in->len != 1) {
		interp_error(wk, ctx->err_node,
			"to use @PLAINNAME@ and @BASENAME@ in a custom "
			"target output, there must be exactly one input");
		return format_cb_error;
	}

	obj in0;
	obj_array_index(wk, ctx->input, 0, &in0);
	const struct str *ss = get_str(wk, *get_obj_file(wk, in0));
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

	obj file = *get_obj_file(wk, v);

	obj s;
	if (!string_format(wk, ctx->err_node, file, &s, ctx, format_cmd_output_cb)) {
		return ir_err;
	}

	obj f;
	make_obj(wk, &f, obj_file);
	*get_obj_file(wk, f) = s;

	obj_array_push(wk, ctx->output, f);

	return ir_cont;
}

struct process_custom_tgt_sources_ctx {
	uint32_t err_node;
	obj tgt_id;
	obj res;
};

static enum iteration_result
process_custom_tgt_sources_iter(struct workspace *wk, void *_ctx, obj val)
{
	obj res;
	struct process_custom_tgt_sources_ctx *ctx = _ctx;

	switch (get_obj_type(wk, val)) {
	case obj_generated_list:
		if (!generated_list_process_for_target(wk, ctx->err_node, val, ctx->tgt_id, true, &res)) {
			return ir_err;
		}
		break;
	default: {
		if (!coerce_files(wk, ctx->err_node, val, &res)) {
			return ir_err;
		}
		break;
	}
	}

	obj_array_extend(wk, ctx->res, res);
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
	const char *output_dir,
	obj command_orig,
	obj depfile_orig,
	bool capture,
	obj *res)
{
	obj input, raw_output, output, args;

	make_obj(wk, res, obj_custom_target);
	struct obj_custom_target *tgt = get_obj_custom_target(wk, *res);
	tgt->name = name;

	// A custom_target won't have a name if it is from a generator
	if (name) { /* private path */
		char path[PATH_MAX] = { 0 };
		if (!path_join(path, PATH_MAX, get_cstr(wk, current_project(wk)->build_dir), get_cstr(wk, name))) {
			return false;
		}

		if (!path_add_suffix(path, PATH_MAX, ".p")) {
			return false;
		}

		tgt->private_path = make_str(wk, path);
	}

	if (input_orig) {
		make_obj(wk, &input, obj_array);

		struct process_custom_tgt_sources_ctx ctx = {
			.err_node = input_node,
			.res = input,
			.tgt_id = *res,
		};

		if (get_obj_type(wk, input_orig) != obj_array) {
			obj arr_input;
			make_obj(wk, &arr_input, obj_array);
			obj_array_push(wk, arr_input, input_orig);
			input_orig = arr_input;
		}

		if (!obj_array_foreach_flat(wk, input_orig, &ctx, process_custom_tgt_sources_iter)) {
			return false;
		} else if (!get_obj_array(wk, input)->len) {
			interp_error(wk, input_node, "input cannot be empty");
		}
	} else {
		make_obj(wk, &input, obj_array);
	}

	if (!coerce_output_files(wk, output_node, output_orig, output_dir, &raw_output)) {
		return false;
	} else if (!get_obj_array(wk, raw_output)->len) {
		interp_error(wk, output_node, "output cannot be empty");
	}

	make_obj(wk, &output, obj_array);
	struct custom_target_cmd_fmt_ctx ctx = {
		.err_node = output_node,
		.input = input,
		.output = output,
		.name = name,
	};
	if (!obj_array_foreach(wk, raw_output, &ctx, custom_command_output_format_iter)) {
		return false;
	}

	obj depends;
	make_obj(wk, &depends, obj_array);
	if (!process_custom_target_commandline(wk, command_node, true, name,
		command_orig, input, output, depfile_orig, depends, &args)) {
		return false;
	}

	if (capture) {
		tgt->flags |= custom_target_capture;
	}

	tgt->args = args;
	tgt->input = input;
	tgt->output = output;
	tgt->depends = depends;
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
		kw_depends,
		kw_build_always_stale,
		kw_env,
	};
	struct args_kw akw[] = {
		[kw_input] = { "input", obj_any, },
		[kw_output] = { "output", obj_any, .required = true },
		[kw_command] = { "command", ARG_TYPE_ARRAY_OF | obj_any, .required = true },
		[kw_capture] = { "capture", obj_bool },
		[kw_install] = { "install", obj_bool },
		[kw_install_dir] = { "install_dir", obj_any },
		[kw_install_mode] = { "install_mode", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_build_by_default] = { "build_by_default", obj_bool },
		[kw_depfile] = { "depfile", obj_string },
		[kw_depend_files] = { "depend_files", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_depends] = { "depends", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_build_always_stale] = { "build_always_stale", obj_bool },
		[kw_env] = { "env", obj_any },
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
		get_cstr(wk, current_project(wk)->build_dir),
		akw[kw_command].val,
		akw[kw_depfile].val,
		akw[kw_capture].set && get_obj_bool(wk, akw[kw_capture].val),
		res
		)) {
		return false;
	}

	struct obj_custom_target *tgt = get_obj_custom_target(wk, *res);

	if (akw[kw_depend_files].set) {
		obj depend_files;
		if (!coerce_files(wk, akw[kw_depend_files].node, akw[kw_depend_files].val, &depend_files)) {
			return false;
		}

		obj_array_extend(wk, tgt->depends, depend_files);
	}

	if (akw[kw_depends].set) {
		obj depends;
		if (!coerce_files(wk, akw[kw_depends].node, akw[kw_depends].val, &depends)) {
			return false;
		}

		obj_array_extend(wk, tgt->depends, depends);
	}

	if (akw[kw_build_always_stale].set && get_obj_bool(wk, akw[kw_build_always_stale].val)) {
		tgt->flags |= custom_target_build_always_stale;
	}

	if (akw[kw_build_by_default].set) {
		if (get_obj_bool(wk, akw[kw_build_by_default].val)) {
			tgt->flags |= custom_target_build_by_default;
		}
	}

	if ((akw[kw_install].set && get_obj_bool(wk, akw[kw_install].val))
	    || (!akw[kw_install].set && akw[kw_install_dir].set)) {
		if (!akw[kw_install_dir].set) {
			interp_error(wk, akw[kw_install].node, "custom target installation requires install_dir");
			return false;
		}

		if (!akw[kw_build_by_default].set) {
			tgt->flags |= custom_target_build_by_default;
		}

		obj install_mode_id = 0;
		if (akw[kw_install_mode].set) {
			install_mode_id = akw[kw_install_mode].val;
		}

		if (!push_install_targets(wk, tgt->output, akw[kw_install_dir].val, install_mode_id)) {
			return false;
		}
	}

	if (akw[kw_env].set) {
		// coerce to envp for typechecking only, we don't use the
		// result here
		char *const *_ = NULL;
		if (!env_to_envp(wk, akw[kw_env].node, &_, akw[kw_env].val, 0)) {
			return false;
		}

		tgt->env = akw[kw_env].val;
	}

	LOG_I("adding custom target '%s'", get_cstr(wk, tgt->name));

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
		fallback = current_project(wk)->cfg.version;
	}

	obj command;
	make_obj(wk, &command, obj_array);

	push_args_null_terminated(wk, command, (char *const []){
		wk->argv0,
		"-C",
		NULL,
	});

	obj_array_push(wk, command, current_project(wk)->build_dir);

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
		get_cstr(wk, current_project(wk)->build_dir),
		command,
		0,
		false,
		res
		)) {
		return false;
	}

	struct obj_custom_target *tgt = get_obj_custom_target(wk, *res);
	tgt->flags |= custom_target_build_always_stale;

	obj_array_push(wk, current_project(wk)->targets, *res);
	return true;
}
