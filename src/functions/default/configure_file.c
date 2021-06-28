#include "posix.h"

#include <string.h>

#include "coerce.h"
#include "filesystem.h"
#include "functions/common.h"
#include "functions/default/configure_file.h"
#include "interpreter.h"
#include "log.h"
#include "mem.h"
#include "path.h"

#define GROW_SIZE 2048
#define TMP_BUF_LEN 1024

static void
buf_push(char **buf, uint64_t *cap, uint64_t *i, const char *str, uint32_t len)
{
	if (*i + len >= *cap) {
		if (len > GROW_SIZE) {
			*cap += len;
		} else {
			*cap += GROW_SIZE;
		}

		*buf = z_realloc(*buf, *cap);
	}

	strncpy(&(*buf)[*i], str, len);
	*i += len;
}

static const char *mesondefine = "#mesondefine ";

static bool
substitute_config(struct workspace *wk, uint32_t dict, uint32_t in_node, const char *in, const char *out)
{
	/* L(log_interp, "in: %s", in); */
	/* L(log_interp, "out: %s", out); */
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
		 line = 1, start_of_line = 0, id_start_col, id_start_line;
	bool reading_id = false;
	uint32_t elem;
	bool found;
	char tmp_buf[TMP_BUF_LEN] = { 0 };

	for (i = 0; i < src.len; ++i) {
		if (src.src[i] == '\n') {
			start_of_line = i + 1;
			++line;
		}

		if (!reading_id && i == start_of_line && strncmp(&src.src[i], mesondefine, mesondefine_len) == 0) {
			if (i > id_end) {
				buf_push(&out_buf, &out_cap, &out_len, &src.src[id_end], i - id_end);
			}

			/* L(log_interp, "%s", out_buf); */

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
			} else if (!obj_dict_index_strn(wk, dict, &src.src[id_start], id_end - id_start, &elem, &found)) {
				error_messagef(&src, id_start_line, id_start_col, "failed to index dict");
				return false; // shouldn't happen tbh
			} else if (!found) {
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
				snprintf(tmp_buf, TMP_BUF_LEN, "%ld", get_obj(wk, elem)->dat.num);
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
				} else if (!obj_dict_index_strn(wk, dict, &src.src[id_start], i - id_start, &elem, &found)) {
					error_messagef(&src, id_start_line, id_start_col, "failed to index dict");
					return false; // shouldn't happen tbh
				} else if (!found) {
					error_messagef(&src, id_start_line, id_start_col, "key not found in configuration data");
					return false;
				}

				if (!typecheck(wk, in_node, elem, obj_string)) {
					return false;
				}

				const char *sub = wk_objstr(wk, elem);
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

	if (!fs_write(out, (uint8_t *)out_buf, out_len)) {
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
		fprintf(ctx->out, "#define %s %ld\n", wk_objstr(wk, key_id), val->dat.num);
		break;
	default:
		interp_error(wk, ctx->node, "invalid type for config data value: '%s'", obj_type_to_s(val->type));
		return ir_err;
	}

	return ir_cont;
}

static bool
generate_config(struct workspace *wk, uint32_t dict, uint32_t node, const char *out_path)
{
	FILE *out;
	if (!(out = fs_fopen(out_path, "wb"))) {
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

bool
func_configure_file(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	enum kwargs {
		kw_configuration,
		kw_input,
		kw_output,
	};
	struct args_kw akw[] = {
		[kw_configuration] = { "configuration", obj_any, .required = true },
		[kw_input] = { "input", },
		[kw_output] = { "output", obj_string, .required = true },
		0
	};

	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	uint32_t dict;

	switch (get_obj(wk, akw[kw_configuration].val)->type) {
	case obj_dict:
		dict = akw[kw_configuration].val;
		break;
	case obj_configuration_data:
		dict = get_obj(wk, akw[kw_configuration].val)->dat.configuration_data.dict;
		break;
	default:
		interp_error(wk, akw[kw_configuration].node, "invalid type for configuration data '%s'",
			obj_type_to_s(get_obj(wk, akw[kw_configuration].val)->type));
		return false;
	}

	{ /* setup out file */
		const char *out = wk_objstr(wk, akw[kw_output].val);
		if (!path_is_basename(out)) {
			interp_error(wk, akw[kw_output].node, "config file output '%s' contains path seperator", out);
			return false;
		}

		if (!fs_mkdir_p(wk_str(wk, current_project(wk)->build_dir))) {
			return false;
		}

		char buf[PATH_MAX];
		if (!path_join(buf, PATH_MAX, wk_str(wk, current_project(wk)->build_dir), out)) {
			return false;
		}

		make_obj(wk, obj, obj_file)->dat.file = wk_str_push(wk, buf);
	}

	if (akw[kw_input].set) {
		uint32_t input_arr, input;
		if (!coerce_files(wk, akw[kw_input].node, akw[kw_input].val, &input_arr)) {
			return false;
		}

		/* NOTE: when meson gets an empty array as the input argument
		 * to configure file, it acts like the input keyword wasn't set.
		 * We throw an error.
		 */
		if (get_obj(wk, input_arr)->dat.arr.len != 1) {
			interp_error(wk, akw[kw_input].node, "configure_file needs exactly one input (got %d)",
				get_obj(wk, input_arr)->dat.arr.len);
			return false;
		}

		if (!obj_array_index(wk, input_arr, 0, &input)) {
			return false;
		}

		if (!substitute_config(wk, dict, akw[kw_input].node,
			wk_file_path(wk, input), wk_file_path(wk, *obj))) {
			return false;
		}
	} else {
		if (!generate_config(wk, dict, akw[kw_input].node, wk_file_path(wk, *obj))) {
			return true;
		}
	}

	return true;
}
