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

static void
configure_file_skip_whitespace(const struct source *src, uint32_t *i)
{
	while (src->src[*i] && is_whitespace_except_newline(src->src[*i])) {
		++(*i);
	}
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
	configure_file_syntax_mesonvar = 0 << 1,
	configure_file_syntax_cmakevar = 1 << 1,
};

static bool
substitute_config(struct workspace *wk,
	uint32_t dict,
	uint32_t in_node,
	const char *in,
	obj out,
	enum configure_file_syntax syntax)
{
	const char *define;
	if (syntax & configure_file_syntax_cmakedefine) {
		define = "#cmakedefine ";
	} else {
		define = "#mesondefine ";
	}

	const char *varstart;
	char varend;
	if (syntax & configure_file_syntax_cmakevar) {
		varstart = "${";
		varend = '}';
	} else {
		varstart = "@";
		varend = '@';
	}
	const uint32_t define_len = strlen(define), varstart_len = strlen(varstart);

	bool ret = true;
	struct source src;

	if (!fs_read_entire_file(in, &src)) {
		ret = false;
		goto cleanup;
	}

	TSTR_manual(out_buf);

	struct source_location location = { 0, 1 }, id_location;
	uint32_t i, id_start, id_len, col = 0, orig_i;
	obj elem;
	char tmp_buf[BUF_SIZE_1k] = { 0 };

	for (i = 0; i < src.len; ++i) {
		location.off = i;
		if (src.src[i] == '\n') {
			col = i + 1;
		}

		if (i == col && strncmp(&src.src[i], define, define_len) == 0) {
			i += define_len;

			configure_file_skip_whitespace(&src, &i);

			id_start = i;
			id_location = location;
			++id_location.off;
			id_len = configure_var_len(&src.src[id_start]);
			i += id_len;

			const char *sub = NULL, *deftype = "#define";

			configure_file_skip_whitespace(&src, &i);

			if (!(src.src[i] == '\n' || src.src[i] == 0)) {
				if (syntax & configure_file_syntax_cmakedefine) {
					if (src.src[i] == '@'
						&& strncmp(&src.src[i + 1], &src.src[id_start], id_len) == 0
						&& src.src[i + 1 + id_len] == '@') {
						i += 2 + id_len;
						configure_file_skip_whitespace(&src, &i);

						if (!(src.src[i] == '\n' || src.src[i] == 0)) {
							goto extraneous_cmake_chars;
						}
					} else {
extraneous_cmake_chars:
						orig_i = i;

						while (src.src[i] && src.src[i] != '\n') {
							++i;
						}

						/* id_location.col = orig_i - location.col + 1; */
						error_messagef(&src,
							id_location,
							log_warn,
							"ignoring trailing characters (%.*s) in cmakedefine",
							i - orig_i,
							&src.src[orig_i]);
					}
				} else {
					/* id_location.col = i - location.col + 1; */
					error_messagef(&src,
						id_location,
						log_error,
						"expected exactly one token on mesondefine line");
					return false;
				}
			}

			if (i == id_start) {
				error_messagef(&src, id_location, log_error, "key of zero length not supported");
				return false;
			} else if (!obj_dict_index_strn(wk, dict, &src.src[id_start], id_len, &elem)) {
				deftype = "/* undef";
				sub = "*/";
				goto write_mesondefine;
			}

			switch (get_obj_type(wk, elem)) {
			case obj_bool: {
				if (!get_obj_bool(wk, elem)) {
					deftype = "#undef";
				}
				break;
			}
			case obj_string: {
				sub = get_cstr(wk, elem);
				break;
			}
			case obj_number:
				snprintf(tmp_buf, BUF_SIZE_1k, "%" PRId64, get_obj_number(wk, elem));
				sub = tmp_buf;
				break;
			default:
				error_messagef(&src,
					id_location,
					log_error,
					"invalid type for %s: '%s'",
					define,
					obj_type_to_s(get_obj_type(wk, elem)));
				return false;
			}

write_mesondefine:
			tstr_pushn(wk, &out_buf, deftype, strlen(deftype));
			tstr_pushn(wk, &out_buf, " ", 1);
			tstr_pushn(wk, &out_buf, &src.src[id_start], id_len);
			if (sub) {
				tstr_pushn(wk, &out_buf, " ", 1);
				tstr_pushn(wk, &out_buf, sub, strlen(sub));
			}

			i -= 1; // so we catch the newline
		} else if (src.src[i] == '\\') {
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

			for (j = 1; src.src[i + j] && src.src[i + j] == '\\'; ++j) {
			}

			if (strncmp(&src.src[i + j], varstart, varstart_len) == 0) {
				output_backslashes = j / 2;

				if (*varstart == '@') {
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
				tstr_pushn(wk, &out_buf, "\\", 1);
			}

			if (output_format_char) {
				tstr_pushn(wk, &out_buf, varstart, varstart_len);
				i += varstart_len - 1;
			}
		} else if (strncmp(&src.src[i], varstart, varstart_len) == 0) {
			i += varstart_len;
			id_start = i;
			id_location = location;
			i += configure_var_len(&src.src[id_start]);

			if (src.src[i] != varend) {
				i = id_start - 1;
				tstr_pushn(wk, &out_buf, varstart, varstart_len);
				continue;
			}

			if (i <= id_start) {
				// This means we got a key of length zero
				tstr_pushs(wk, &out_buf, "@@");
				continue;
			} else if (!obj_dict_index_strn(wk, dict, &src.src[id_start], i - id_start, &elem)) {
				error_messagef(&src, id_location, log_error, "key not found in configuration data");
				return false;
			}

			obj sub;
			if (!coerce_string(wk, in_node, elem, &sub)) {
				error_messagef(&src, id_location, log_error, "unable to substitute value");
				return false;
			}

			const struct str *ss = get_str(wk, sub);
			tstr_pushn(wk, &out_buf, ss->s, ss->len);
		} else {
			tstr_pushn(wk, &out_buf, &src.src[i], 1);
		}
	}

	if (file_exists_with_content(wk, get_cstr(wk, out), out_buf.buf, out_buf.len)) {
		goto cleanup;
	}

	if (!fs_write(get_cstr(wk, out), (uint8_t *)out_buf.buf, out_buf.len)) {
		ret = false;
		goto cleanup;
	}

	if (!fs_copy_metadata(in, get_cstr(wk, out))) {
		ret = false;
		goto cleanup;
	}

cleanup:
	fs_source_destroy(&src);
	tstr_destroy(&out_buf);
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

	if (!path_chdir(get_cstr(wk, current_project(wk)->build_dir))) {
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
			vm_error_at(wk, akw[kw_output].node, "config file output '%s' contains path separator", out);
			return false;
		}

		if (!fs_mkdir_p(get_cstr(wk, current_project(wk)->build_dir))) {
			return false;
		}

		path_join(wk, &out_path, get_cstr(wk, current_project(wk)->build_dir), out);

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
					syntax = configure_file_syntax_cmakedefine | configure_file_syntax_cmakevar;
				} else if (str_eql(fmt, &STR("cmake@"))) {
					syntax = configure_file_syntax_cmakedefine | configure_file_syntax_mesonvar;
				} else {
					vm_error_at(
						wk, akw[kw_format].node, "invalid format type %o", akw[kw_format].val);
					return false;
				}
			}

			if (!substitute_config(wk, dict, akw[kw_input].node, path, output_str, syntax)) {
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
