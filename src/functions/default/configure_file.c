#include "posix.h"

#include <inttypes.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "coerce.h"
#include "error.h"
#include "functions/common.h"
#include "functions/default/configure_file.h"
#include "functions/default/custom_target.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

static void
abuf_push(char **buf, uint64_t *cap, uint64_t *i, const char *str, uint32_t len)
{
	if (*i + len >= *cap) {
		if (len > BUF_SIZE_2k) {
			*cap += len;
		} else {
			*cap += BUF_SIZE_2k;
		}

		*buf = z_realloc(*buf, *cap);
	}

	strncpy(&(*buf)[*i], str, len);
	*i += len;
	(*buf)[*i] = 0;
}

static const char *mesondefine = "#mesondefine ";

static bool
substitute_config(struct workspace *wk, uint32_t dict, uint32_t in_node, const char *in, uint32_t out)
{
	/* L("in: %s", in); */
	/* L("out: %s", out); */
	const uint32_t mesondefine_len = strlen(mesondefine);

	bool ret = true;
	char *out_buf = NULL;
	struct source src;
	uint64_t out_len, out_cap;

	if (!fs_read_entire_file(in, &src)) {
		ret = false;
		goto cleanup;
	}

	out_len = 0;
	out_cap = src.len;
	out_buf = z_calloc(out_cap, 1);

	uint32_t i, id_start, id_end = 0,
		 line = 1, start_of_line = 0, id_start_col = 0, id_start_line = 0;
	uint32_t elem;
	char tmp_buf[BUF_SIZE_1k] = { 0 };

	for (i = 0; i < src.len; ++i) {
		/* L("%c '%s'", src.src[i], out_buf); */
		if (src.src[i] == '\n') {
			start_of_line = i + 1;
			++line;
		}

		if (i == start_of_line && strncmp(&src.src[i], mesondefine, mesondefine_len) == 0) {
			/* L("%s", out_buf); */

			i += mesondefine_len;
			id_start = i;
			id_start_line = line;
			id_start_col = i - start_of_line;

			char *end = strchr(&src.src[id_start], '\n');
			const char *sub = NULL, *deftype = "#define";

			if (!end) {
				error_messagef(&src, id_start_line, id_start_col, "got EOF while scanning #mesondefine");
				return false;
			}

			id_end = end - src.src;
			i = id_end - 1;
			++line;

			strncpy(tmp_buf, &src.src[id_start], id_end - id_start);
			tmp_buf[i - id_start + 1] = 0;

			if (id_end - id_start == 0) {
				error_messagef(&src, id_start_line, id_start_col, "key of zero length not supported");
				return false;
			} else if (!obj_dict_index_strn(wk, dict, &src.src[id_start], id_end - id_start, &elem)) {
				deftype = "/* undef";
				sub = "*/";
				goto write_mesondefine;
			}

			switch (get_obj(wk, elem)->type) {
			case obj_bool: {
				if (!get_obj(wk, elem)->dat.boolean) {
					deftype = "#undef";
				}
				break;
			}
			case obj_string: {
				sub = get_cstr(wk, elem);
				break;
			}
			case obj_number:
				snprintf(tmp_buf, BUF_SIZE_1k, "%" PRId64, get_obj(wk, elem)->dat.num);
				sub = tmp_buf;
				break;
			default:
				error_messagef(&src, id_start_line, id_start_col,
					"invalid type for #mesondefine: '%s'",
					obj_type_to_s(get_obj(wk, elem)->type));
				return false;
			}

write_mesondefine:
			abuf_push(&out_buf, &out_cap, &out_len, deftype, strlen(deftype));
			abuf_push(&out_buf, &out_cap, &out_len, " ", 1);
			abuf_push(&out_buf, &out_cap, &out_len, &src.src[id_start], id_end - id_start);
			if (sub) {
				abuf_push(&out_buf, &out_cap, &out_len, " ", 1);
				abuf_push(&out_buf, &out_cap, &out_len, sub, strlen(sub));
			}
		} else if (src.src[i] == '\\') {
			/* cope with weird config file escaping rules :(
			 *
			 * - Backslashes not directly preceeding a format character are not modified.
			 * - The number of backslashes preceding a @ in the
			 *   output is equal to the number of backslashes in
			 *   the input divided by two, rounding down.
			 */

			uint32_t j, output_backslashes;
			bool output_format_char = false;

			for (j = 1; src.src[i + j] && src.src[i + j] == '\\'; ++j) {
			}

			if (src.src[i + j] == '@') {
				output_backslashes = j / 2;
				if ((j & 1) != 0) {
					output_format_char = true;
					i += j;
				} else {
					i += j - 1;
				}
			} else {
				i += j - 1;

				output_backslashes = j;
			}

			for (j = 0; j < output_backslashes; ++j) {
				abuf_push(&out_buf, &out_cap, &out_len, "\\", 1);
			}

			if (output_format_char) {
				abuf_push(&out_buf, &out_cap, &out_len, "@", 1);
			}
		} else if (src.src[i] == '@') {
			// Only allow (a-z, A-Z, 0-9, _, -) as valid characters for a define

			++i;
			id_start_line = line;
			id_start = i;
			id_start_col = id_start - start_of_line + 1;

			while (('a' <= src.src[i] && src.src[i] <= 'z')
			       || ('A' <= src.src[i] && src.src[i] <= 'Z')
			       || ('0' <= src.src[i] && src.src[i] <= '9')
			       || '_' == src.src[i]
			       || '-' == src.src[i]
			       ) {
				++i;
			}

			id_end = i - 1;

			if (src.src[i] != '@') {
				i = id_start - 1;
				abuf_push(&out_buf, &out_cap, &out_len, "@", 1);
				continue;
			}

			if (i == id_start) {
				error_messagef(&src, id_start_line, id_start_col, "key of zero length not supported");
				return false;
			} else if (!obj_dict_index_strn(wk, dict, &src.src[id_start], i - id_start, &elem)) {
				error_messagef(&src, id_start_line, id_start_col, "key not found in configuration data");
				return false;
			}

			obj sub;
			if (!coerce_string(wk, in_node, elem, &sub)) {
				error_messagef(&src, id_start_line, id_start_col, "unable to substitute value");
				return false;
			}

			const struct str *ss = get_str(wk, sub);
			abuf_push(&out_buf, &out_cap, &out_len, ss->s, ss->len);
		} else {
			abuf_push(&out_buf, &out_cap, &out_len, &src.src[i], 1);
		}
	}

	if (!fs_write(get_cstr(wk, out), (uint8_t *)out_buf, out_len)) {
		ret = false;
		goto cleanup;
	}

cleanup:
	fs_source_destroy(&src);

	if (out_buf) {
		z_free(out_buf);
	}
	return ret;
}

struct generate_config_ctx {
	FILE *out;
	uint32_t node;
};

static enum iteration_result
generate_config_iter(struct workspace *wk, void *_ctx, uint32_t key_id, uint32_t val_id)
{
	struct generate_config_ctx *ctx = _ctx;
	struct obj *val = get_obj(wk, val_id);

	switch (val->type) {
	case obj_string:
		/* conf_data.set('FOO', '"string"') => #define FOO "string" */
		/* conf_data.set('FOO', 'a_token')  => #define FOO a_token */
		fprintf(ctx->out, "#define %s %s\n", get_cstr(wk, key_id), get_cstr(wk, val_id));
		break;
	case obj_bool:
		/* conf_data.set('FOO', true)       => #define FOO */
		/* conf_data.set('FOO', false)      => #undef FOO */
		if (val->dat.boolean) {
			fprintf(ctx->out, "#define %s\n", get_cstr(wk, key_id));
		} else {
			fprintf(ctx->out, "#undef %s\n", get_cstr(wk, key_id));
		}
		break;
	case obj_number:
		/* conf_data.set('FOO', 1)          => #define FOO 1 */
		/* conf_data.set('FOO', 0)          => #define FOO 0 */
		fprintf(ctx->out, "#define %s %" PRId64 "\n", get_cstr(wk, key_id), val->dat.num);
		break;
	default:
		interp_error(wk, ctx->node, "invalid type for config data value: '%s'", obj_type_to_s(val->type));
		return ir_err;
	}

	return ir_cont;
}

static bool
generate_config(struct workspace *wk, uint32_t dict, uint32_t node, uint32_t out_path)
{
	FILE *out;
	if (!(out = fs_fopen(get_cstr(wk, out_path), "wb"))) {
		return false;
	}

	struct generate_config_ctx ctx = {
		.out = out,
		.node = node,
	};

	bool ret;
	ret = obj_dict_foreach(wk, dict, &ctx, generate_config_iter);

	if (!fs_fclose(out)) {
		return false;
	}

	return ret;
}

static bool
configure_file_with_command(struct workspace *wk, uint32_t node,
	obj command, obj input, obj out_path, obj depfile,
	bool capture)
{
	uint32_t args, output_arr;

	{
		uint32_t f;
		make_obj(wk, &f, obj_file)->dat.file = out_path;
		make_obj(wk, &output_arr, obj_array);
		obj_array_push(wk, output_arr, f);
	}

	obj depends; // used only for the call below :(
	make_obj(wk, &depends, obj_array);
	if (!process_custom_target_commandline(wk, node, 0, command, input,
		output_arr, depfile, depends, &args)) {
		return false;
	}

	const char *argv[MAX_ARGS];
	if (!join_args_argv(wk, argv, MAX_ARGS, args)) {
		interp_error(wk, node, "failed to prepare arguments");
		return false;
	}

	bool ret = false;
	struct run_cmd_ctx cmd_ctx = { 0 };
	char *const *envp;

	if (!path_chdir(get_cstr(wk, current_project(wk)->build_dir))) {
		return false;
	}

	if (!env_to_envp(wk, 0, &envp, 0, env_to_envp_flag_subdir)) {
		goto ret;
	} else if (!run_cmd(&cmd_ctx, argv[0], argv, envp)) {
		interp_error(wk, node, "error running command: %s", cmd_ctx.err_msg);
		goto ret;
	}

	if (cmd_ctx.status != 0) {
		interp_error(wk, node, "error running command: %s", cmd_ctx.err.buf);
		goto ret;
	}

	if (capture) {
		ret = fs_write(get_cstr(wk, out_path), (uint8_t *)cmd_ctx.out.buf, cmd_ctx.out.len);
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

	struct obj *a = get_obj(wk, arr);

	if (a->dat.arr.len != 1) {
		interp_error(wk, node, "expected an array of length 1");
		return false;
	}

	obj_array_index(wk, arr, 0, res);
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
	uint32_t str = make_str(wk, ""), e = 0, len;

	for (; *s; ++s) {
		if (is_substr(s, "@BASENAME@", &len)) {
			if (!array_to_elem_or_err(wk, node, input_arr, &e)) {
				return false;
			}
			assert(e);

			char buf[PATH_MAX], *c;
			if (!path_basename(buf, PATH_MAX, wk_file_path(wk, e))) {
				return false;
			}

			if ((c = strrchr(buf, '.'))) {
				*c = 0;
			}

			str_app(wk, str, buf);
			s += len - 1;
		} else if (is_substr(s, "@PLAINNAME@", &len)) {
			if (!array_to_elem_or_err(wk, node, input_arr, &e)) {
				return false;
			}

			char buf[PATH_MAX];
			if (!path_basename(buf, PATH_MAX, wk_file_path(wk, e))) {
				return false;
			}

			str_app(wk, str, buf);
			s += len - 1;
		} else {
			str_appn(wk, str, s, 1);
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
func_configure_file(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	uint32_t input_arr = 0, output_str;
	enum kwargs {
		kw_configuration,
		kw_input,
		kw_output,
		kw_command,
		kw_capture,
		kw_install,
		kw_install_dir,
		kw_install_mode,
		kw_copy,
		kw_encoding, // TODO: ignored
		kw_depfile, // TODO: ignored
	};
	struct args_kw akw[] = {
		[kw_configuration] = { "configuration", obj_any },
		[kw_input] = { "input", obj_any, },
		[kw_output] = { "output", obj_string, .required = true },
		[kw_command] = { "command", obj_array },
		[kw_capture] = { "capture", obj_bool },
		[kw_install] = { "install", obj_bool },
		[kw_install_dir] = { "install_dir", obj_string },
		[kw_install_mode] = { "install_mode", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_copy] = { "copy", obj_bool },
		[kw_encoding] = { "encoding", obj_string },
		[kw_depfile] = { "depfile", obj_string },
		0
	};

	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	if (akw[kw_input].set) {
		if (!coerce_files(wk, akw[kw_input].node, akw[kw_input].val, &input_arr)) {
			return false;
		}
	} else {
		// set this so we can use it in error handling later
		akw[kw_input].node = args_node;
	}

	{       /* setup out file */
		uint32_t subd;
		if (!perform_output_string_substitutions(wk, akw[kw_output].node,
			akw[kw_output].val, input_arr, &subd)) {
			return false;
		}

		const char *out = get_cstr(wk, subd);
		char out_path[PATH_MAX];


		if (!path_is_basename(out)) {
			interp_error(wk, akw[kw_output].node, "config file output '%s' contains path seperator", out);
			return false;
		}

		if (!fs_mkdir_p(get_cstr(wk, current_project(wk)->build_dir))) {
			return false;
		}

		if (!path_join(out_path, PATH_MAX, get_cstr(wk, current_project(wk)->build_dir), out)) {
			return false;
		}

		LOG_I("configuring '%s", out_path);
		output_str = make_str(wk, out_path);
		make_obj(wk, res, obj_file)->dat.file = output_str;
	}

	if (!exclusive_or((bool []) { akw[kw_command].set, akw[kw_configuration].set, akw[kw_copy].set }, 3)) {
		interp_error(wk, args_node, "you must pass either command:, configuration:, or copy:");
		return false;
	}

	if (akw[kw_command].set) {
		bool capture = false;
		if (akw[kw_capture].set) {
			capture = get_obj(wk, akw[kw_capture].val)->dat.boolean;
		}

		if (!configure_file_with_command(wk, akw[kw_command].node,
			akw[kw_command].val, input_arr, output_str,
			akw[kw_depfile].val, capture)) {
			return false;
		}
	} else if (akw[kw_copy].set) {
		uint32_t input;

		if (!array_to_elem_or_err(wk, akw[kw_input].node, input_arr, &input)) {
			return false;
		}

		struct source src = { 0 };
		if (!fs_read_entire_file(wk_file_path(wk, input), &src)) {
			return false;
		}

		if (!fs_write(get_cstr(wk, output_str), (uint8_t *)src.src, src.len)) {
			fs_source_destroy(&src);
			return false;
		}

		fs_source_destroy(&src);
	} else {
		uint32_t dict, conf = akw[kw_configuration].val;

		switch (get_obj(wk, conf)->type) {
		case obj_dict:
			dict = conf;
			break;
		case obj_configuration_data:
			dict = get_obj(wk, conf)->dat.configuration_data.dict;
			break;
		default:
			interp_error(wk, akw[kw_configuration].node, "invalid type for configuration data '%s'",
				obj_type_to_s(get_obj(wk, conf)->type));
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

			const char *path;
			switch (get_obj(wk, input)->type) {
			case obj_file:
				path = wk_file_path(wk, input);
				break;
			case obj_string:
				path = get_cstr(wk, input);
				break;
			default:
				interp_error(wk, akw[kw_input].node, "unable to coerce input to file");
				return false;
			}

			if (!substitute_config(wk, dict, akw[kw_input].node,
				path, output_str)) {
				return false;
			}
		} else {
			if (!generate_config(wk, dict, akw[kw_configuration].node, output_str)) {
				return false;
			}
		}
	}

	if ((akw[kw_install].set && get_obj(wk, akw[kw_install].val)->dat.boolean)
	    || (!akw[kw_install].set && akw[kw_install_dir].set)) {
		if (!akw[kw_install_dir].set) {
			interp_error(wk, akw[kw_install].node, "configure_file installation requires install_dir");
			return false;
		}

		push_install_target_install_dir(wk, output_str, akw[kw_install_dir].val, akw[kw_install_mode].val);
	}

	return true;
}
