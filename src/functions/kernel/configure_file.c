/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "coerce.h"
#include "error.h"
#include "functions/environment.h"
#include "functions/kernel/configure_file.h"
#include "functions/kernel/custom_target.h"
#include "install.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

enum configure_file_output_format {
	configure_file_output_format_c,
	configure_file_output_format_nasm,
	configure_file_output_format_json,
};

static bool
file_exists_with_content(struct workspace *wk, const char *dest, const char *out_buf, uint32_t out_len)
{
	if (fs_file_exists(dest)) {
		struct source src = { 0 };
		if (fs_read_entire_file(dest, &src)) {
			bool eql = out_len == src.len && memcmp(out_buf, src.src, src.len) == 0;

			fs_source_destroy(&src);

			return eql;
		}
	}

	return false;
}

static uint32_t
configure_var_len(const char *p)
{
	uint32_t i = 0;

	// Only allow (a-z, A-Z, 0-9, _, -) as valid characters for a define
	while (p[i]
		&& (('a' <= p[i] && p[i] <= 'z') || ('A' <= p[i] && p[i] <= 'Z') || ('0' <= p[i] && p[i] <= '9')
			|| '_' == p[i] || '-' == p[i])) {
		++i;
	}

	return i;
}

enum configure_file_syntax {
	configure_file_syntax_mesondefine = 0 << 0,
	configure_file_syntax_cmakedefine = 1 << 0,

	configure_file_syntax_mesonvar = 1 << 1,
	configure_file_syntax_cmakevar = 1 << 2,
};

struct configure_file_context {
	const char *name;
	obj dict;
	const struct tstr *in;
	enum configure_file_syntax syntax;
};

struct configure_file_var_patterns {
	struct {
		struct str start, end;
		enum configure_file_syntax type;
	} pats[2];
	uint32_t len;
};

static void
configure_file_var_pattern_push(struct configure_file_var_patterns *var_patterns,
	enum configure_file_syntax syntax,
	enum configure_file_syntax enable_flag,
	const struct str *start,
	const struct str *end)
{
	if (!(syntax & enable_flag)) {
		return;
	}

	assert(var_patterns->len < ARRAY_LEN(var_patterns->pats));

	var_patterns->pats[var_patterns->len].start = *start;
	var_patterns->pats[var_patterns->len].end = *end;
	var_patterns->pats[var_patterns->len].type = enable_flag;

	++var_patterns->len;
}

MUON_ATTR_FORMAT(printf, 4, 5)
static void
configure_file_log(struct configure_file_context *ctx,
	struct source_location loc,
	enum log_level lvl,
	const char *fmt,
	...)
{
	va_list args;
	va_start(args, fmt);

	struct source src = {
		.src = ctx->in->buf,
		.len = ctx->in->len,
		.label = ctx->name,
	};

	error_messagev(&src, loc, lvl, fmt, args);

	va_end(args);
}

static bool
configure_file_var_patterns_match(struct configure_file_var_patterns *var_patterns,
	const struct tstr *in,
	uint32_t off,
	uint32_t *match_idx)
{
	const struct str s = { in->buf + off, in->len - off };

	for (uint32_t i = 0; i < var_patterns->len; ++i) {
		if (str_startswith(&s, &var_patterns->pats[i].start)) {
			*match_idx = i;
			return true;
		}
	}
	return false;
}

static bool
substitute_config_variables(struct workspace *wk, struct configure_file_context *ctx, struct tstr *out)
{
	const struct tstr *in = ctx->in;

	struct configure_file_var_patterns var_patterns = { 0 };
	configure_file_var_pattern_push(
		&var_patterns, ctx->syntax, configure_file_syntax_cmakevar, &STR("${"), &STR("}"));
	configure_file_var_pattern_push(
		&var_patterns, ctx->syntax, configure_file_syntax_mesonvar, &STR("@"), &STR("@"));

	struct source_location location = { 0, 1 }, id_location;
	uint32_t i, id_start;
	uint32_t match_idx = 0;
	obj elem;

	for (i = 0; i < in->len; ++i) {
		location.off = i;

		struct str src_str = { in->buf + i, in->len - i };

		if (in->buf[i] == '\\') {
			/* cope with weird config file escaping rules :(
			 *
			 * - Backslashes not directly preceeding a format character are not
			 *   modified.
			 * - The number of backslashes preceding varstart in the output is
			 *   equal to the number of backslashes in the input divided by
			 *   two, rounding down.
			 * - If mode is cmake and the number of backslashes is even, don't
			 *   escape the variable, otherwise always escape the variable.
			 */

			uint32_t j, output_backslashes;
			bool output_format_char = false;

			for (j = 1; in->buf[i + j] == '\\'; ++j) {
			}

			if (configure_file_var_patterns_match(&var_patterns, in, i + j, &match_idx)) {
				output_backslashes = j / 2;

				if (var_patterns.pats[match_idx].type == configure_file_syntax_mesonvar) {
					output_format_char = true;
					i += j;
				} else {
					if ((j & 1) != 0) {
						output_format_char = true;
						i += j;
					} else {
						i += j - 1;
					}
				}
			} else {
				i += j - 1;

				output_backslashes = j;
			}

			for (j = 0; j < output_backslashes; ++j) {
				tstr_pushn(wk, out, "\\", 1);
			}

			if (output_format_char) {
				tstr_pushs(wk, out, var_patterns.pats[match_idx].start.s);
				i += var_patterns.pats[match_idx].start.len - 1;
			}
		} else if (configure_file_var_patterns_match(&var_patterns, in, i, &match_idx)) {
			i += var_patterns.pats[match_idx].start.len;
			id_start = i;
			id_location = location;
			i += configure_var_len(&in->buf[id_start]);

			src_str = (struct str){ in->buf + i, in->len - i };
			if (!str_startswith(&src_str, &var_patterns.pats[match_idx].end)) {
				i = id_start - 1;
				tstr_pushs(wk, out, var_patterns.pats[match_idx].start.s);
				continue;
			}

			if (i <= id_start) {
				// This means we got a key of length zero
				tstr_pushs(wk, out, "@@");
				continue;
			} else if (!obj_dict_index_strn(wk, ctx->dict, &in->buf[id_start], i - id_start, &elem)) {
				configure_file_log(ctx,
					id_location,
					log_error,
					"key %.*s not found in configuration data",
					i - id_start,
					&in->buf[id_start]);
				return false;
			}

			obj sub;
			if (!coerce_string(wk, 0, elem, &sub)) {
				configure_file_log(ctx, id_location, log_error, "unable to substitute value");
				return false;
			}

			const struct str *ss = get_str(wk, sub);
			tstr_pushn(wk, out, ss->s, ss->len);
		} else {
			tstr_pushn(wk, out, &in->buf[i], 1);
		}
	}

	return true;
}

static bool
substitute_config_defines(struct workspace *wk, struct configure_file_context *ctx, struct tstr *out)
{
	struct str define = { 0 };
	if (ctx->syntax & configure_file_syntax_cmakedefine) {
		define = STR("#cmakedefine");
	} else {
		define = STR("#mesondefine");
	}

	struct source_location location = { 0, 1 };
	obj elem;

	const char *e, *s = ctx->in->buf, *eof = s + ctx->in->len;

	while (s < eof) {
		if (!(e = strchr(s, '\n'))) {
			e = eof;
		}

		location.off = s - ctx->in->buf;

		struct str line = { s, e - s };

		if (str_startswith(&line, &define)) {
			obj arr = str_split(wk, &line, 0);

			if (ctx->syntax & configure_file_syntax_cmakedefine) {
				bool cmake_bool = str_startswith(&line, &STR("#cmakedefine01"));

				if (get_obj_array(wk, arr)->len < 2) {
					configure_file_log(
						ctx, location, log_error, "#cmakedefine does not contain >= 2 tokens");
					return false;
				}

				obj varname = obj_array_index(wk, arr, 1);
				if (!obj_dict_index(wk, ctx->dict, varname, &elem)) {
					if (cmake_bool) {
						tstr_pushf(wk, out, "#define %s 0\n", get_str(wk, varname)->s);
					} else {
						tstr_pushf(wk, out, "/* #undef %s */\n", get_str(wk, varname)->s);
					}
				} else {
					if (!cmake_bool && !coerce_truthiness(wk, elem)) {
						tstr_pushf(wk, out, "/* #undef %s */\n", get_str(wk, varname)->s);
					} else {
						tstr_pushf(wk, out, "#define %s", get_str(wk, varname)->s);

						obj v;
						obj_array_for_(wk, arr, v, iter)
						{
							if (iter.i < 2) {
								continue;
							}

							if (obj_dict_index(wk, ctx->dict, v, &elem)) {
								obj str;
								if (!coerce_string(wk, 0, v, &str)) {
									return false;
								}
								tstr_pushf(wk, out, " %s", get_cstr(wk, str));
							} else {
								tstr_pushf(wk, out, " %s", get_str(wk, v)->s);
							}
						}

						tstr_push(wk, out, '\n');
					}
				}
			} else {
				if (get_obj_array(wk, arr)->len != 2) {
					configure_file_log(ctx,
						location,
						log_error,
						"#mesondefine does not contain exactly two tokens");
					return false;
				}

				obj varname = obj_array_index(wk, arr, 1);
				if (!obj_dict_index(wk, ctx->dict, varname, &elem)) {
					tstr_pushf(wk, out, "/* #undef %s */\n", get_str(wk, varname)->s);
				} else {
					if (!typecheck(wk, 0, elem, tc_string | tc_bool | tc_number)) {
						return false;
					}

					switch (get_obj_type(wk, elem)) {
					case obj_string: {
						tstr_pushf(wk,
							out,
							"#define %s %s\n",
							get_str(wk, varname)->s,
							get_cstr(wk, elem));
						break;
					}
					case obj_bool: {
						if (get_obj_bool(wk, elem)) {
							tstr_pushf(wk, out, "#define %s\n", get_str(wk, varname)->s);
						} else {
							tstr_pushf(wk, out, "#undef %s\n", get_str(wk, varname)->s);
						}
						break;
					}
					case obj_number: {
						tstr_pushf(wk,
							out,
							"#define %s %" PRId64 "\n",
							get_str(wk, varname)->s,
							get_obj_number(wk, elem));
						break;
					}
					default: UNREACHABLE;
					}
				}
			}
		} else {
			tstr_pushn(wk, out, line.s, line.len);
			tstr_push(wk, out, '\n');
		}

		s = e + 1;
	}

	return true;
}

static bool
substitute_config(struct workspace *wk,
	obj dict,
	const char *input_path,
	const char *output_path,
	enum configure_file_syntax syntax)
{
	bool ret = true;
	struct source src = { 0 };

	if (!fs_read_entire_file(input_path, &src)) {
		ret = false;
		goto cleanup;
	}

	const struct tstr src_tstr = { (char *)src.src, src.len };

	struct configure_file_context ctx = {
		.name = input_path,
		.dict = dict,
		.in = &src_tstr,
		.syntax = syntax,
	};

	TSTR(out1);
	if (!substitute_config_defines(wk, &ctx, &out1)) {
		ret = false;
		goto cleanup;
	}

	ctx.in = &out1;
	TSTR(out2);
	if (!substitute_config_variables(wk, &ctx, &out2)) {
		ret = false;
		goto cleanup;
	}

	if (file_exists_with_content(wk, output_path, out2.buf, out2.len)) {
		goto cleanup;
	}

	if (!fs_write(output_path, (uint8_t *)out2.buf, out2.len)) {
		ret = false;
		goto cleanup;
	}

	if (!fs_copy_metadata(input_path, output_path)) {
		ret = false;
		goto cleanup;
	}

cleanup:
	fs_source_destroy(&src);
	return ret;
}

static bool
generate_config(struct workspace *wk,
	enum configure_file_output_format format,
	obj macro_name,
	obj dict,
	uint32_t node,
	obj out_path)
{
	TSTR_manual(out_buf);

	if (macro_name) {
		tstr_pushf(
			wk, &out_buf, "#ifndef %s\n#define %s\n", get_cstr(wk, macro_name), get_cstr(wk, macro_name));
	} else if (format == configure_file_output_format_json) {
		tstr_push(wk, &out_buf, '{');
	}

	bool ret = false, first = true;

	obj key, val;
	obj_dict_for(wk, dict, key, val) {
		enum obj_type t = get_obj_type(wk, val);

		char define_prefix = (char[]){
			[configure_file_output_format_c] = '#',
			[configure_file_output_format_nasm] = '%',
			[configure_file_output_format_json] = 0,
		}[format];

		if (format == configure_file_output_format_json) {
			if (!first) {
				tstr_push(wk, &out_buf, ',');
			}
			first = false;

			tstr_push_json_escaped_quoted(wk, &out_buf, get_str(wk, key));
			tstr_push(wk, &out_buf, ':');
		}

		switch (t) {
		case obj_string:
			/* conf_data.set('FOO', '"string"') => #define FOO "string" */
			/* conf_data.set('FOO', 'a_token')  => #define FOO a_token */
			switch (format) {
			case configure_file_output_format_c:
			case configure_file_output_format_nasm:
				tstr_pushf(wk,
					&out_buf,
					"%cdefine %s %s\n",
					define_prefix,
					get_cstr(wk, key),
					get_cstr(wk, val));
				break;
			case configure_file_output_format_json: {
				tstr_push_json_escaped_quoted(wk, &out_buf, get_str(wk, val));
				break;
			}
			}
			break;
		case obj_bool:
			/* conf_data.set('FOO', true)       => #define FOO */
			/* conf_data.set('FOO', false)      => #undef FOO */
			switch (format) {
			case configure_file_output_format_c:
			case configure_file_output_format_nasm:
				if (get_obj_bool(wk, val)) {
					tstr_pushf(wk, &out_buf, "%cdefine %s\n", define_prefix, get_cstr(wk, key));
				} else {
					tstr_pushf(wk, &out_buf, "%cundef %s\n", define_prefix, get_cstr(wk, key));
				}
				break;
			case configure_file_output_format_json:
				tstr_pushs(wk, &out_buf, get_obj_bool(wk, val) ? "true" : "false");
				break;
			}
			break;
		case obj_number:
			/* conf_data.set('FOO', 1)          => #define FOO 1 */
			/* conf_data.set('FOO', 0)          => #define FOO 0 */
			switch (format) {
			case configure_file_output_format_c:
			case configure_file_output_format_nasm:
				tstr_pushf(wk,
					&out_buf,
					"%cdefine %s %" PRId64 "\n",
					define_prefix,
					get_cstr(wk, key),
					get_obj_number(wk, val));
				break;
			case configure_file_output_format_json: {
				char buf[32] = { 0 };
				snprintf(buf, sizeof(buf), "%" PRId64, get_obj_number(wk, val));
				tstr_pushs(wk, &out_buf, buf);
				break;
			}
			}
			break;
		default: vm_error_at(wk, node, "invalid type for config data value: '%s'", obj_type_to_s(t)); goto ret;
		}
	}

	if (macro_name) {
		tstr_pushf(wk, &out_buf, "#endif\n");
	} else if (format == configure_file_output_format_json) {
		tstr_pushs(wk, &out_buf, "}\n");
	}

	if (!file_exists_with_content(wk, get_cstr(wk, out_path), out_buf.buf, out_buf.len)) {
		if (!fs_write(get_cstr(wk, out_path), (uint8_t *)out_buf.buf, out_buf.len)) {
			goto ret;
		}
	}

	ret = true;
ret:
	tstr_destroy(&out_buf);
	return ret;
}

static bool
configure_file_with_command(struct workspace *wk,
	uint32_t node,
	obj command,
	obj input,
	obj out_path,
	obj depfile,
	bool capture)
{
	obj args, output_arr;

	{
		obj f;
		f = make_obj(wk, obj_file);
		*get_obj_file(wk, f) = out_path;
		output_arr = make_obj(wk, obj_array);
		obj_array_push(wk, output_arr, f);
	}

	{
		// XXX: depfile for configure_file is not supported, this is
		// only here to make the types align
		obj f;
		f = make_obj(wk, obj_file);
		*get_obj_file(wk, f) = depfile;
		depfile = f;
	}

	struct process_custom_target_commandline_opts opts = {
		.err_node = node,
		.input = input,
		.output = output_arr,
		.depfile = depfile,
	};
	opts.depends = make_obj(wk, obj_array);

	if (!process_custom_target_commandline(wk, &opts, command, &args)) {
		return false;
	}

	bool ret = false;
	struct run_cmd_ctx cmd_ctx = { 0 };
	const char *argstr, *envstr;
	uint32_t argc, envc;

	if (!path_chdir(workspace_build_dir(wk))) {
		return false;
	}

	obj env;
	env = make_obj(wk, obj_dict);
	set_default_environment_vars(wk, env, true);

	join_args_argstr(wk, &argstr, &argc, args);
	env_to_envstr(wk, &envstr, &envc, env);
	if (!run_cmd(&cmd_ctx, argstr, argc, envstr, envc)) {
		vm_error_at(wk, node, "error running command: %s", cmd_ctx.err_msg);
		goto ret;
	}

	if (cmd_ctx.status != 0) {
		vm_error_at(wk, node, "error running command: %s", cmd_ctx.err.buf);
		goto ret;
	}

	if (capture) {
		if (file_exists_with_content(wk, get_cstr(wk, out_path), cmd_ctx.out.buf, cmd_ctx.out.len)) {
			ret = true;
		} else {
			ret = fs_write(get_cstr(wk, out_path), (uint8_t *)cmd_ctx.out.buf, cmd_ctx.out.len);
		}
	} else {
		ret = true;
	}
ret:
	if (!path_chdir(wk->source_root)) {
		return false;
	}

	run_cmd_ctx_destroy(&cmd_ctx);
	return ret;
}

static bool
array_to_elem_or_err(struct workspace *wk, uint32_t node, uint32_t arr, uint32_t *res)
{
	if (!typecheck(wk, node, arr, obj_array)) {
		return false;
	}

	if (get_obj_array(wk, arr)->len != 1) {
		vm_error_at(wk, node, "expected an array of length 1");
		return false;
	}

	*res = obj_array_index(wk, arr, 0);
	return true;
}

static bool
is_substr(const char *s, const char *sub, uint32_t *len)
{
	*len = strlen(sub);
	return strncmp(s, sub, *len) == 0;
}

static bool
perform_output_string_substitutions(struct workspace *wk, uint32_t node, uint32_t src, uint32_t input_arr, uint32_t *res)
{
	const char *s = get_cstr(wk, src);
	uint32_t len;
	obj str = make_str(wk, ""), e = 0;

	for (; *s; ++s) {
		if (is_substr(s, "@BASENAME@", &len)) {
			if (!array_to_elem_or_err(wk, node, input_arr, &e)) {
				return false;
			}
			assert(e);

			TSTR(buf);
			char *c;
			path_basename(wk, &buf, get_file_path(wk, e));

			if ((c = strrchr(buf.buf, '.'))) {
				*c = 0;
				buf.len = strlen(buf.buf);
			}

			str_app(wk, &str, buf.buf);
			s += len - 1;
		} else if (is_substr(s, "@PLAINNAME@", &len)) {
			if (!array_to_elem_or_err(wk, node, input_arr, &e)) {
				return false;
			}

			TSTR(buf);
			path_basename(wk, &buf, get_file_path(wk, e));
			str_app(wk, &str, buf.buf);
			s += len - 1;
		} else {
			str_appn(wk, &str, s, 1);
		}
	}

	*res = str;
	return true;
}

static bool
exclusive_or(bool *vals, uint32_t len)
{
	uint32_t i;
	bool found = false;
	for (i = 0; i < len; ++i) {
		if (vals[i]) {
			if (found) {
				return false;
			} else {
				found = true;
			}
		}
	}

	return found;
}

bool
func_configure_file(struct workspace *wk, obj _, obj *res)
{
	obj input_arr = 0, output_str;
	enum kwargs {
		kw_configuration,
		kw_input,
		kw_output,
		kw_command,
		kw_capture,
		kw_install,
		kw_install_dir,
		kw_install_mode,
		kw_install_tag,
		kw_copy,
		kw_format,
		kw_output_format,
		kw_encoding, // TODO: ignored
		kw_depfile, // TODO: ignored
		kw_macro_name,
	};
	struct args_kw akw[] = {
		[kw_configuration] = { "configuration", tc_configuration_data | tc_dict },
		[kw_input] = { "input", TYPE_TAG_LISTIFY | tc_coercible_files, },
		[kw_output] = { "output", obj_string, .required = true },
		[kw_command] = { "command", obj_array },
		[kw_capture] = { "capture", obj_bool },
		[kw_install] = { "install", obj_bool },
		[kw_install_dir] = { "install_dir", obj_string },
		[kw_install_mode] = { "install_mode", tc_install_mode_kw },
		[kw_install_tag] = { "install_tag", obj_string }, // TODO
		[kw_copy] = { "copy", obj_bool },
		[kw_format] = { "format", obj_string },
		[kw_output_format] = { "output_format", obj_string },
		[kw_encoding] = { "encoding", obj_string },
		[kw_depfile] = { "depfile", obj_string },
		[kw_macro_name] = { "macro_name", obj_string },
		0,
	};

	if (!pop_args(wk, NULL, akw)) {
		return false;
	}

	enum configure_file_output_format output_format = configure_file_output_format_c;
	if (akw[kw_output_format].set) {
		const struct str *output_format_str = get_str(wk, akw[kw_output_format].val);
		if (str_eql(output_format_str, &STR("c"))) {
			output_format = configure_file_output_format_c;
		} else if (str_eql(output_format_str, &STR("nasm"))) {
			output_format = configure_file_output_format_nasm;
		} else if (str_eql(output_format_str, &STR("json"))) {
			output_format = configure_file_output_format_json;
		} else {
			vm_error_at(
				wk, akw[kw_output_format].node, "invalid output format %o", akw[kw_output_format].val);
			return false;
		}
	}

	if (akw[kw_input].set) {
		if (!coerce_files(wk, akw[kw_input].node, akw[kw_input].val, &input_arr)) {
			return false;
		}
	}

	{ /* setup out file */
		obj subd;
		if (!perform_output_string_substitutions(
			    wk, akw[kw_output].node, akw[kw_output].val, input_arr, &subd)) {
			return false;
		}

		const char *out = get_cstr(wk, subd);
		TSTR(out_path);

		if (!path_is_basename(out)) {
			if (wk->vm.lang_mode == language_external) {
				vm_error_at(wk,
					akw[kw_output].node,
					"config file output '%s' contains path separator",
					out);
				return false;
			}

			TSTR(dir);
			path_dirname(wk, &dir, out);
			if (!fs_mkdir_p(dir.buf)) {
				return false;
			}
		}

		if (!fs_mkdir_p(workspace_build_dir(wk))) {
			return false;
		}

		path_join(wk, &out_path, workspace_build_dir(wk), out);

		LOG_I("configuring '%s'", out_path.buf);
		output_str = tstr_into_str(wk, &out_path);
		*res = make_obj(wk, obj_file);
		*get_obj_file(wk, *res) = output_str;
	}

	if (!exclusive_or((bool[]){ akw[kw_command].set, akw[kw_configuration].set, akw[kw_copy].set }, 3)) {
		vm_error(wk, "you must pass either command:, configuration:, or copy:");
		return false;
	}

	if (akw[kw_command].set) {
		bool capture = akw[kw_capture].set && get_obj_bool(wk, akw[kw_capture].val);

		if (!configure_file_with_command(wk,
			    akw[kw_command].node,
			    akw[kw_command].val,
			    input_arr,
			    output_str,
			    akw[kw_depfile].val,
			    capture)) {
			return false;
		}
	} else if (akw[kw_copy].set) {
		obj input;
		bool copy_res = false;

		if (!array_to_elem_or_err(wk, akw[kw_input].node, input_arr, &input)) {
			return false;
		}

		workspace_add_regenerate_deps(wk, *get_obj_file(wk, input));

		struct source src = { 0 };
		if (!fs_read_entire_file(get_file_path(wk, input), &src)) {
			return false;
		}

		if (!file_exists_with_content(wk, get_cstr(wk, output_str), src.src, src.len)) {
			if (!fs_write(get_cstr(wk, output_str), (uint8_t *)src.src, src.len)) {
				goto copy_err;
			}

			if (!fs_copy_metadata(get_file_path(wk, input), get_cstr(wk, output_str))) {
				goto copy_err;
			}
		}

		copy_res = true;
copy_err:
		fs_source_destroy(&src);
		if (!copy_res) {
			return false;
		}
	} else {
		obj dict, conf = akw[kw_configuration].val;

		enum obj_type t = get_obj_type(wk, conf);
		switch (t) {
		case obj_dict: dict = conf; break;
		case obj_configuration_data: dict = get_obj_configuration_data(wk, conf)->dict; break;
		default:
			vm_error_at(wk,
				akw[kw_configuration].node,
				"invalid type for configuration data '%s'",
				obj_type_to_s(t));
			return false;
		}

		if (akw[kw_input].set) {
			obj input;

			/* NOTE: when meson gets an empty array as the input argument
			 * to configure file, it acts like the input keyword wasn't set.
			 * We throw an error.
			 */
			if (!array_to_elem_or_err(wk, akw[kw_input].node, input_arr, &input)) {
				return false;
			}

			workspace_add_regenerate_deps(wk, *get_obj_file(wk, input));
			const char *path = get_file_path(wk, input);

			enum configure_file_syntax syntax = configure_file_syntax_mesondefine
							    | configure_file_syntax_mesonvar;

			if (akw[kw_format].set) {
				const struct str *fmt = get_str(wk, akw[kw_format].val);
				if (str_eql(fmt, &STR("meson"))) {
					syntax = configure_file_syntax_mesondefine | configure_file_syntax_mesonvar;
				} else if (str_eql(fmt, &STR("cmake"))) {
					syntax = configure_file_syntax_cmakedefine | configure_file_syntax_cmakevar
						 | configure_file_syntax_mesonvar;
				} else if (str_eql(fmt, &STR("cmake@"))) {
					syntax = configure_file_syntax_cmakedefine | configure_file_syntax_mesonvar;
				} else {
					vm_error_at(
						wk, akw[kw_format].node, "invalid format type %o", akw[kw_format].val);
					return false;
				}
			}

			if (!substitute_config(wk, dict, path, get_cstr(wk, output_str), syntax)) {
				return false;
			}
		} else {
			if (akw[kw_macro_name].set && output_format != configure_file_output_format_c) {
				vm_error_at(
					wk, akw[kw_macro_name].node, "macro_name specified with a non c output format");
				return false;
			}

			if (!generate_config(wk,
				    output_format,
				    akw[kw_macro_name].val,
				    dict,
				    akw[kw_configuration].node,
				    output_str)) {
				return false;
			}
		}
	}

	if ((akw[kw_install].set && get_obj_bool(wk, akw[kw_install].val))
		|| (!akw[kw_install].set && akw[kw_install_dir].set)) {
		if (!akw[kw_install_dir].set) {
			vm_error_at(wk, akw[kw_install].node, "configure_file installation requires install_dir");
			return false;
		}

		push_install_target_install_dir(wk, output_str, akw[kw_install_dir].val, akw[kw_install_mode].val);
	}

	return true;
}
