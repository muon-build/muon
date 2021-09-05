#include "posix.h"

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
buf_push(char **buf, uint64_t *cap, uint64_t *i, const char *str, uint32_t len)
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
	out_buf = z_malloc(out_cap);

	uint32_t i, id_start, id_end = 0,
		 line = 1, start_of_line = 0, id_start_col = 0, id_start_line = 0;
	bool reading_id = false;
	uint32_t elem;
	char tmp_buf[BUF_SIZE_1k] = { 0 };

	for (i = 0; i < src.len; ++i) {
		if (src.src[i] == '\n') {
			start_of_line = i + 1;
			++line;
		}

		if (!reading_id && i == start_of_line && strncmp(&src.src[i], mesondefine, mesondefine_len) == 0) {
			if (i > id_end) {
				buf_push(&out_buf, &out_cap, &out_len, &src.src[id_end], i - id_end);
			}

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
				sub = wk_objstr(wk, elem);
				break;
			}
			case obj_number:
				snprintf(tmp_buf, BUF_SIZE_1k, "%ld", (intmax_t)get_obj(wk, elem)->dat.num);
				sub = tmp_buf;
				break;
			default:
				error_messagef(&src, id_start_line, id_start_col,
					"invalid type for #mesondefine: '%s'",
					obj_type_to_s(get_obj(wk, elem)->type));
				return false;
			}

write_mesondefine:
			buf_push(&out_buf, &out_cap, &out_len, deftype, strlen(deftype));
			buf_push(&out_buf, &out_cap, &out_len, " ", 1);
			buf_push(&out_buf, &out_cap, &out_len, &src.src[id_start], id_end - id_start);
			if (sub) {
				buf_push(&out_buf, &out_cap, &out_len, " ", 1);
				buf_push(&out_buf, &out_cap, &out_len, sub, strlen(sub));
			}
		} else if (src.src[i] == '@') {
			if (reading_id) {
				id_end = i + 1;

				if (i == id_start) {
					error_messagef(&src, id_start_line, id_start_col, "key of zero length not supported");
					return false;
				} else if (!obj_dict_index_strn(wk, dict, &src.src[id_start], i - id_start, &elem)) {
					error_messagef(&src, id_start_line, id_start_col, "key not found in configuration data");
					return false;
				}

				const char *sub = NULL;
				if (!coerce_string(wk, in_node, elem, &sub)) {
					error_messagef(&src, id_start_line, id_start_col, "unable to substitue value");
					return false;
				}

				buf_push(&out_buf, &out_cap, &out_len, sub, strlen(sub));
				reading_id = false;
			} else {
				if (i) {
					buf_push(&out_buf, &out_cap, &out_len, &src.src[id_end], i - id_end);
				}

				id_start_line = line;
				id_start = i + 1;
				id_start_col = id_start - start_of_line + 1;
				reading_id = true;
			}
		}
	}

	if (reading_id) {
		error_messagef(&src, id_start_line, id_start_col - 1, "missing closing '@'");
		return false;
	}

	if (i > id_end) {
		buf_push(&out_buf, &out_cap, &out_len, &src.src[id_end], i - id_end);
	}

	if (!fs_write(wk_str(wk, out), (uint8_t *)out_buf, out_len)) {
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
		fprintf(ctx->out, "#define %s %s\n", wk_objstr(wk, key_id), wk_objstr(wk, val_id));
		break;
	case obj_bool:
		/* conf_data.set('FOO', true)       => #define FOO */
		/* conf_data.set('FOO', false)      => #undef FOO */
		if (val->dat.boolean) {
			fprintf(ctx->out, "#define %s\n", wk_objstr(wk, key_id));
		} else {
			fprintf(ctx->out, "#undef %s\n", wk_objstr(wk, key_id));
		}
		break;
	case obj_number:
		/* conf_data.set('FOO', 1)          => #define FOO 1 */
		/* conf_data.set('FOO', 0)          => #define FOO 0 */
		fprintf(ctx->out, "#define %s %ld\n", wk_objstr(wk, key_id), (intmax_t)val->dat.num);
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
	if (!(out = fs_fopen(wk_str(wk, out_path), "wb"))) {
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
	uint32_t command, uint32_t input, uint32_t out_path, uint32_t depfile,
	bool capture)
{
	uint32_t args, output_arr;

	{
		uint32_t f;
		make_obj(wk, &f, obj_file)->dat.file = out_path;
		make_obj(wk, &output_arr, obj_array);
		obj_array_push(wk, output_arr, f);
	}

	if (!process_custom_target_commandline(wk, node, command, input,
		output_arr, depfile, &args)) {
		return false;
	}

	char *argv[MAX_ARGS];
	if (!join_args_argv(wk, argv, MAX_ARGS, args)) {
		interp_error(wk, node, "failed to prepare arguments");
		return false;
	}

	bool ret = false;
	struct run_cmd_ctx cmd_ctx = { 0 };
	char *const *envp;

	if (!build_envp(wk, &envp, build_envp_flag_subdir)) {
		goto ret;
	} else if (!run_cmd(&cmd_ctx, argv[0], argv, envp)) {
		interp_error(wk, node, "error running command: %s", cmd_ctx.err_msg);
		goto ret;
	}

	if (cmd_ctx.status != 0) {
		interp_error(wk, node, "error running command: %s", cmd_ctx.err);
		goto ret;
	}

	if (capture) {
		ret = fs_write(wk_str(wk, out_path), (uint8_t *)cmd_ctx.out, strlen(cmd_ctx.out));
	} else {
		ret = true;
	}
ret:
	run_cmd_ctx_destroy(&cmd_ctx);
	return ret;
}

bool
func_configure_file(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
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
		[kw_depfile] = { "depfile", obj_string },
		0
	};

	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	{         /* setup out file */
		const char *out = wk_objstr(wk, akw[kw_output].val);
		char out_path[PATH_MAX];

		if (!path_is_basename(out)) {
			interp_error(wk, akw[kw_output].node, "config file output '%s' contains path seperator", out);
			return false;
		}

		if (!fs_mkdir_p(wk_str(wk, current_project(wk)->build_dir))) {
			return false;
		}

		if (!path_join(out_path, PATH_MAX, wk_str(wk, current_project(wk)->build_dir), out)) {
			return false;
		}

		output_str = wk_str_push(wk, out_path);
		make_obj(wk, obj, obj_file)->dat.file = output_str;
	}

	if ((akw[kw_command].set && akw[kw_configuration].set)
	    || (!akw[kw_command].set && !akw[kw_configuration].set)) {
		interp_error(wk, args_node, "you must pass either command: or configuration:");
		return false;
	}

	if (akw[kw_input].set) {
		if (!coerce_files(wk, akw[kw_input].node, akw[kw_input].val, &input_arr)) {
			return false;
		}
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
			uint32_t input;

			/* NOTE: when meson gets an empty array as the input argument
			 * to configure file, it acts like the input keyword wasn't set.
			 * We throw an error.
			 */
			if ((akw[kw_command].set && get_obj(wk, input_arr)->dat.arr.len == 0)
			    || get_obj(wk, input_arr)->dat.arr.len != 1) {
				interp_error(wk, akw[kw_input].node, "configure_file with configuration: needs exactly one input (got %d), or omit the input keyword",
					get_obj(wk, input_arr)->dat.arr.len);
				return false;
			}

			if (!obj_array_index(wk, input_arr, 0, &input)) {
				return false;
			}
			if (!substitute_config(wk, dict, akw[kw_input].node,
				wk_file_path(wk, input), output_str)) {
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

		push_install_target(wk, 0, output_str, akw[kw_install_dir].val, 0);
	}

	return true;
}
