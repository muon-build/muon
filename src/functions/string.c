#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "functions/common.h"
#include "functions/string.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
chr_in_str(char c, const struct str *ss)
{
	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if (ss->s[i] == c) {
			return true;
		}
	}

	return false;
}

static bool
func_strip(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, NULL, ao, NULL)) {
		return false;
	}

	const struct str *strip = ao[0].set ? get_str(wk, ao[0].val) : &WKSTR(" \n");

	uint32_t i;
	int32_t len;
	const struct str *ss = get_str(wk, rcvr);

	for (i = 0; i < ss->len; ++i) {
		if (!chr_in_str(ss->s[i], strip)) {
			break;
		}
	}

	for (len = ss->len - 1; len >= 0; --len) {
		if (!chr_in_str(ss->s[i], strip)) {
			break;
		}
	}
	++len;

	make_obj(wk, obj, obj_string)->dat.str = wk_str_pushn(wk, &ss->s[i], len - i);
	return true;
}

static bool
func_to_upper(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, get_cstr(wk, rcvr));

	char *s = get_cstr(wk, *obj);

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

	uint32_t in_len = strlen(get_cstr(wk, str));

	uint32_t i, id_start, id_end = 0;
	bool reading_id = false;

	*out = wk_str_push(wk, "");
	in = get_cstr(wk, str);

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
				case format_cb_found: {
					const char *coerced;
					if (!coerce_string(wk, node, elem, &coerced)) {
						return false;
					}
					wk_str_app(wk, out, coerced);
					in = get_cstr(wk, str);
					break;
				}
				case format_cb_skip: {
					wk_str_appf(wk, out, "@%s@", key);
					in = get_cstr(wk, str);
					break;
				}
				}

				reading_id = false;
			} else {
				if (i) {
					wk_str_appn(wk, out, &in[id_end], i - id_end);
					in = get_cstr(wk, str);
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

static enum format_cb_result
func_format_cb(struct workspace *wk, uint32_t node, void *_ctx, const char *key, uint32_t *elem)
{
	struct func_format_ctx *ctx = _ctx;
	char *endptr;
	int64_t i = strtol(key, &endptr, 10);

	if (*endptr) {
		return format_cb_skip;
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

static bool
func_underscorify(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	uint32_t s_id = wk_str_push(wk, get_cstr(wk, rcvr));
	char *s = get_cstr(wk, s_id);

	for (; *s; ++s) {
		if (!(('a' <= *s && *s <= 'z')
		      || ('A' <= *s && *s <= 'Z')
		      || ('0' <= *s && *s <= '9'))) {
			*s = '_';
		}
	}

	make_obj(wk, obj, obj_string)->dat.str = s_id;
	return true;
}

static bool
func_split(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, NULL, ao, NULL)) {
		return false;
	}

	uint32_t i, start = 0, seplen, s_id;
	static char sep[BUF_SIZE_2k + 1] = { 0 };

	if (ao[0].set) {
		strncpy(sep, get_cstr(wk, ao[0].val), BUF_SIZE_2k);
	} else {
		strncpy(sep, " ", BUF_SIZE_2k);
	}
	seplen = strlen(sep);

	make_obj(wk, obj, obj_array);

	const char *str = get_cstr(wk, rcvr);
	for (i = 0; str[i]; ++i) {
		if (strncmp(&str[i], sep, seplen) == 0) {
			make_obj(wk, &s_id, obj_string)->dat.str =
				wk_str_pushn(wk, &str[start], i - start);
			str = get_cstr(wk, rcvr); // str may have been moved by the above line

			obj_array_push(wk, *obj, s_id);

			start = i + seplen;
			i += seplen - 1;
		}
	}

	make_obj(wk, &s_id, obj_string)->dat.str =
		wk_str_pushn(wk, &str[start], i - start);

	obj_array_push(wk, *obj, s_id);

	return true;
}

static bool
func_join(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	return obj_array_join(wk, an[0].val, rcvr, obj);
}

static bool
str_to_uint32(struct workspace *wk, uint32_t error_node, const char *s, uint32_t *output)
{
	char *endptr = NULL;
	int32_t n = strtol(s, &endptr, 10);
	if (*endptr) {
		interp_error(wk, error_node, "nondigit in version core: %s", s);
		return false;
	}

	if (n < 0) {
		interp_error(wk, error_node, "negative digit in version core: %s", s);
		return false;
	}

	*output = n;

	return true;
}

#define MAX_VER_BUF_SIZE_2k 64

static bool
str_to_version(struct workspace *wk, uint32_t error_node, struct version *v, const char *str)
{
	char buf[MAX_VER_BUF_SIZE_2k];
	uint32_t n = 0;
	size_t j = 0;
	uint32_t num = 0;
	for (size_t i = 0; str[i]; i++) {
		if (!(j < MAX_VER_BUF_SIZE_2k)) {
			interp_error(wk, error_node, "exceeded maximum buffer length when parsing semantic version");
			return false;
		}

		if (str[i] == '.') {
			buf[j] = '\0';
			if (!str_to_uint32(wk, error_node, buf, &num)) {
				return false;
			}

			if (n == 0) {
				v->major = num;
			} else if (n == 1) {
				v->minor = num;
			}

			n++;
			buf[0] = '\0';
			j = 0;
			continue;
		}

		buf[j] = str[i];
		j++;
	}

	if (j < MAX_VER_BUF_SIZE_2k) {
		buf[j] = '\0';
	}

	if (!str_to_uint32(wk, error_node, buf, &num)) {
		return false;
	}

	v->patch = num;

	return true;
}

static bool
op_gt(uint32_t a, uint32_t b)
{
	return a > b;
}

static bool
op_ge(uint32_t a, uint32_t b)
{
	return a >= b;
}

static bool
op_lt(uint32_t a, uint32_t b)
{
	return a < b;
}

static bool
op_le(uint32_t a, uint32_t b)
{
	return a <= b;
}

static bool
op_eq(uint32_t a, uint32_t b)
{
	return a == b;
}

static bool
op_ne(uint32_t a, uint32_t b)
{
	return a != b;
}

typedef bool ((*comparator)(uint32_t a, uint32_t b));

static bool
string_version_compare(struct workspace *wk, uint32_t rcvr, const char *str, const char *str_arg, uint32_t str_arg_node, bool *output)
{
	struct version v = { 0, 0, 0 };
	if (!str_to_version(wk, rcvr, &v, str)) {
		interp_error(wk, rcvr, "invalid version string");
		return false;
	}

	comparator op = op_eq;

	static struct {
		const char *name;
		comparator op;
	} ops[] = {
		{ ">=", op_ge, },
		{ ">",  op_gt, },
		{ "==", op_eq, },
		{ "!=", op_ne, },
		{ "<=", op_le, },
		{ "<",  op_lt, },
		{ "=", op_eq, },
		NULL
	};

	uint32_t i, op_len = 0;
	for (i = 0; ops[i].name; ++i) {
		op_len = strlen(ops[i].name);

		if (!strncmp(str_arg, ops[i].name, op_len)) {
			str_arg = &str_arg[op_len];
			op = ops[i].op;
			break;
		}
	}

	struct version v_arg = { 0, 0, 0 };

	if (!str_to_version(wk, str_arg_node, &v_arg, str_arg)) {
		interp_error(wk, str_arg_node, "invalid version string");
		return false;
	}

	if (v.major != v_arg.major) {
		*output = op(v.major, v_arg.major);
		goto ret;
	}

	if (v.minor != v_arg.minor) {
		*output = op(v.minor, v_arg.minor);
		goto ret;
	}

	if (v.patch != v_arg.patch) {
		*output = op(v.patch, v_arg.patch);
		goto ret;
	}

	if (op == op_eq || op == op_ge || op == op_le) {
		*output = true;
		goto ret;
	}

	*output = false;

ret:
	return true;
}

static bool
func_version_compare(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *str = get_cstr(wk, rcvr);
	const char *str_arg = get_cstr(wk, an[0].val);
	struct obj *res = make_obj(wk, obj, obj_bool);
	if (!string_version_compare(wk, rcvr, str, str_arg, an[0].node, &res->dat.boolean)) {
		return false;
	}

	return true;
}

static bool
func_string_to_int(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *s = get_cstr(wk, rcvr);
	char *endptr = NULL;
	int64_t n = strtol(s, &endptr, 10);
	if (*endptr) {
		interp_error(wk, args_node, "unable to parse '%s'", s);
		return false;
	}

	make_obj(wk, obj, obj_number)->dat.num = n;
	return true;
}

const struct func_impl_name impl_tbl_string[] = {
	{ "strip", func_strip },
	{ "to_upper", func_to_upper },
	{ "to_int", func_string_to_int },
	{ "format", func_format },
	{ "underscorify", func_underscorify },
	{ "split", func_split },
	{ "join", func_join },
	{ "version_compare", func_version_compare },
	{ NULL, NULL },
};
