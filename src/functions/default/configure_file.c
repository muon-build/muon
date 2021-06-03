#include "posix.h"

#include <string.h>

#include "coerce.h"
#include "filesystem.h"
#include "functions/common.h"
#include "functions/default/configure_file.h"
#include "interpreter.h"
#include "log.h"
#include "mem.h"

#define GROW_SIZE 2048

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

static bool
substitute_config(struct workspace *wk, uint32_t conf_data, uint32_t in_node, const char *in, const char *out)
{
	L(log_interp, "in: %s", in);
	L(log_interp, "out: %s", out);

	uint32_t dict = get_obj(wk, conf_data)->dat.configuration_data.dict;
	bool ret = true;
	char *in_buf = NULL, *out_buf = NULL;
	uint64_t in_len, out_len, out_cap;

	if (!fs_read_entire_file(in, &in_buf, &in_len)) {
		ret = false;
		goto cleanup;
	}

	out_len = 0;
	out_cap = in_len;
	out_buf = z_malloc(out_cap);

	uint32_t i, id_start, id_end = 0,
		 line = 1, start_of_line = 0, id_start_col, id_start_line;
	bool reading_id = false;

	for (i = 0; i < in_len; ++i) {
		if (in_buf[i] == '\n') {
			start_of_line = i + 1;
			++line;
		}

		if (in_buf[i] == '@') {
			if (reading_id) {
				uint32_t elem;
				bool found;
				id_end = i + 1;

				if (i == id_start) {
					error_messagef(in, id_start_line, id_start_col, "key of zero length not supported");
					return false;
				} else if (!obj_dict_index_strn(wk, dict, &in_buf[id_start], i - id_start, &elem, &found)) {
					error_messagef(in, id_start_line, id_start_col, "failed to index dict");
					return false; // shouldn't happen tbh
				} else if (!found) {
					error_messagef(in, id_start_line, id_start_col, "key not found in configuration data");
					return false;
				}

				if (!typecheck(wk, in_node, elem, obj_string)) {
					return false;
				}

				const char *sub = wk_objstr(wk, elem);
				L(log_interp, "%s", sub);
				buf_push(&out_buf, &out_cap, &out_len, sub, strlen(sub));

				reading_id = false;
			} else {
				if (i) {
					buf_push(&out_buf, &out_cap, &out_len, &in_buf[id_end], i - id_end);
				}

				id_start_line = line;
				id_start = i + 1;
				id_start_col = id_start - start_of_line + 1;
				reading_id = true;
			}
		}
	}

	if (reading_id) {
		error_messagef(in, id_start_line, id_start_col - 1, "missing closing '@'");
		return false;
	}

	if (i > id_end) {
		buf_push(&out_buf, &out_cap, &out_len, &in_buf[id_end], i - id_end);
	}

	if (!fs_write(out, (uint8_t *)out_buf, out_len)) {
		ret = false;
		goto cleanup;
	}

cleanup:
	if (in_buf) {
		z_free(in_buf);
	}

	if (out_buf) {
		z_free(out_buf);
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
		[kw_configuration] = { "configuration", obj_configuration_data, .required = true },
		[kw_input] = { "input", },
		[kw_output] = { "output", obj_string, .required = true },
		0
	};

	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}


	if (akw[kw_input].set) {
		uint32_t input_arr, input;
		if (!coerce_files(wk, &akw[kw_input], &input_arr)) {
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

		const char *out = wk_objstr(wk, akw[kw_output].val), *p;
		for (p = out; *p; ++p) {
			if (*p == '/') {
				interp_error(wk, akw[kw_output].node, "config file output cannot contain '/'");
				return false;
			}
		}

		make_obj(wk, obj, obj_file)->dat.file =
			wk_str_pushf(wk, "%s/%s", wk_str(wk, current_project(wk)->build_dir), out);

		if (!substitute_config(wk, akw[kw_configuration].val, akw[kw_input].node,
			wk_file_path(wk, input), wk_file_path(wk, *obj))) {
			return false;
		}
	} else {
		interp_error(wk, args_node, "TODO: input config file with no input");
		return false;
	}

	return true;
}
