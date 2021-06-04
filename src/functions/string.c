#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "functions/common.h"
#include "functions/string.h"
#include "interpreter.h"
#include "log.h"

static bool
func_strip(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push_stripped(wk, wk_objstr(wk, rcvr));
	return true;
}

static bool
func_to_upper(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, wk_objstr(wk, rcvr));

	char *s = wk_objstr(wk, *obj);

	for (; *s; ++s) {
		if ('a' <= *s && *s <= 'z') {
			*s -= 32;
		}
	}

	return true;
}

#define MAX_KEY_LEN 64

bool
string_format(struct workspace *wk, uint32_t node, uint32_t str, uint32_t *out, void *ctx, string_format_cb cb)
{
	char *in;
	char key[MAX_KEY_LEN + 1] = { 0 };

	uint32_t in_len = strlen(wk_str(wk, str));

	uint32_t i, id_start, id_end = 0;
	bool reading_id = false;

	*out = wk_str_push(wk, "");
	in = wk_str(wk, str);

	for (i = 0; i < in_len; ++i) {

		if (in[i] == '@') {
			if (reading_id) {
				uint32_t elem;
				id_end = i + 1;

				if (i == id_start) {
					interp_error(wk, node, "key of zero length not supported");
					return false;
				} else if (i - id_start >= MAX_KEY_LEN) {
					interp_error(wk, node, "key is too long (max: %d)", MAX_KEY_LEN);
					return false;
				}

				strncpy(key, &in[id_start], i - id_start);

				switch (cb(wk, node, ctx, key, &elem)) {
				case format_cb_not_found:
					interp_error(wk, node, "key '%s' not found", key);
					return false;
				case format_cb_error:
					return false;
				case format_cb_found:
					break;
				}

				if (!typecheck(wk, node, elem, obj_string)) {
					return false;
				}

				wk_str_app(wk, out, wk_objstr(wk, elem));
				in = wk_str(wk, str);
				reading_id = false;
			} else {
				if (i) {
					wk_str_appn(wk, out, &in[id_end], i - id_end);
					in = wk_str(wk, str);
				}

				id_start = i + 1;
				reading_id = true;
			}
		}
	}

	if (reading_id) {
		interp_error(wk, node, "missing closing '@'");
		return false;
	}

	if (i > id_end) {
		wk_str_appn(wk, out, &in[id_end], i - id_end);
	}

	return true;
}

struct func_format_ctx {
	uint32_t arr;
};

enum format_cb_result
func_format_cb(struct workspace *wk, uint32_t node, void *_ctx, const char *key, uint32_t *elem)
{
	struct func_format_ctx *ctx = _ctx;
	char *endptr;
	int64_t i = strtol(key, &endptr, 10);

	if (*endptr) {
		interp_error(wk, node, "key is not an integer");
		return format_cb_error;
	} else if (!boundscheck(wk, node, ctx->arr, &i)) {
		return format_cb_error;
	} else if (!obj_array_index(wk, ctx->arr, i, elem)) {
		return format_cb_error;
	}

	return format_cb_found;
}

static bool
func_format(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	struct func_format_ctx ctx = {
		.arr = an[0].val,
	};


	uint32_t str;
	if (!string_format(wk, an[0].node, get_obj(wk, rcvr)->dat.str, &str, &ctx, func_format_cb)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = str;
	return true;
}

const struct func_impl_name impl_tbl_string[] = {
	{ "strip", func_strip },
	{ "to_upper", func_to_upper },
	{ "format", func_format },
	{ NULL, NULL },
};
