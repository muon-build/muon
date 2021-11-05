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
		if (len < (int64_t)i) {
			break;
		}

		if (!chr_in_str(ss->s[len], strip)) {
			break;
		}
	}
	++len;

	assert((int64_t)len >= (int64_t)i);
	make_obj(wk, obj, obj_string)->dat.str = wk_str_pushn(wk, &ss->s[i], len - i);
	return true;
}

static bool
func_to_upper(struct workspace *wk, uint32_t rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_string)->dat.str = str_clone(wk, wk, rcvr);

	const struct str *ss = get_str(wk, *res);

	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if ('a' <= ss->s[i] && ss->s[i] <= 'z') {
			((char *)ss->s)[i] -= 32;
		}
	}

	return true;
}

static bool
func_to_lower(struct workspace *wk, uint32_t rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_string)->dat.str = str_clone(wk, wk, rcvr);

	const struct str *ss = get_str(wk, *res);

	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if ('A' <= ss->s[i] && ss->s[i] <= 'Z') {
			((char *)ss->s)[i] += 32;
		}
	}

	return true;
}

#define MAX_KEY_LEN 64

bool
string_format(struct workspace *wk, uint32_t err_node, str s_in, str *s_out, void *ctx, string_format_cb cb)
{
	struct str key;
	const struct str *ss_in = get_str(wk, s_in);

	uint32_t i, id_start, id_end = 0;
	bool reading_id = false;

	*s_out = wk_str_push(wk, "");

	for (i = 0; i < ss_in->len; ++i) {
		if (ss_in->s[i] == '@') {
			if (reading_id) {
				uint32_t elem;
				id_end = i + 1;

				if (i == id_start) {
					interp_error(wk, err_node, "key of zero length not supported");
					return false;
				} else if (i - id_start >= MAX_KEY_LEN) {
					interp_error(wk, err_node, "key is too long (max: %d)", MAX_KEY_LEN);
					return false;
				}

				key = (struct str){ .s = &ss_in->s[id_start], .len = i - id_start };

				switch (cb(wk, err_node, ctx, &key, &elem)) {
				case format_cb_not_found: {
					interp_error(wk, err_node, "key '%.*s' not found", key.len, key.s);
					return false;
				}
				case format_cb_error:
					return false;
				case format_cb_found: {
					obj coerced;
					if (!coerce_string(wk, err_node, elem, &coerced)) {
						return false;
					}

					const struct str *ss = get_str(wk, coerced);
					wk_str_appn(wk, s_out, ss->s, ss->len);
					break;
				}
				case format_cb_skip: {
					wk_str_app(wk, s_out, "@");
					wk_str_appn(wk, s_out, key.s, key.len);
					id_start = i + 1;
					continue;
				}
				}

				reading_id = false;
			} else {
				if (i) {
					wk_str_appn(wk, s_out, &ss_in->s[id_end], i - id_end);
				}

				id_start = i + 1;
				reading_id = true;
				key.len = 0;
			}
		}
	}

	if (reading_id) {
		wk_str_app(wk, s_out, "@");
		wk_str_appn(wk, s_out, key.s, key.len);
	} else {
		if (i > id_end) {
			wk_str_appn(wk, s_out, &ss_in->s[id_end], i - id_end);
		}
	}

	return true;
}

struct func_format_ctx {
	uint32_t arr;
};

static enum format_cb_result
func_format_cb(struct workspace *wk, uint32_t node, void *_ctx, const struct str *key, uint32_t *elem)
{
	struct func_format_ctx *ctx = _ctx;
	int64_t i;

	if (!wk_str_to_i(key, &i)) {
		return format_cb_skip;
	}

	if (!boundscheck(wk, node, ctx->arr, &i)) {
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
func_underscorify(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_string)->dat.str = str_clone(wk, wk, rcvr);

	const struct str *ss = get_str(wk, *res);

	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if (!(('a' <= ss->s[i] && ss->s[i] <= 'z')
		      || ('A' <= ss->s[i] && ss->s[i] <= 'Z')
		      || ('0' <= ss->s[i] && ss->s[i] <= '9'))) {
			((char *)ss->s)[i] = '_';
		}
	}

	return true;
}

static bool
func_split(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, NULL, ao, NULL)) {
		return false;
	}

	const struct str *split = ao[0].set ? get_str(wk, ao[0].val) : &WKSTR(" "),
			 *ss = get_str(wk, rcvr);


	*obj = wk_str_split(wk, ss, split);
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

bool
string_to_version(struct workspace *wk, struct version *v, const struct str *ss)
{
	uint32_t i;
	uint32_t n = 0;
	bool last = false;

	*v = (struct version) { 0 };

	struct str strnum = { .s = ss->s };

	for (i = 0; i < ss->len; ++i) {
		if (ss->s[i] == '.' || (last = (i == ss->len - 1))) {
			int64_t res;

			if (last) {
				++strnum.len;
			}

			if (!wk_str_to_i(&strnum, &res)) {
				return false;
			} else if (res < 0) {
				return false;
			}

			if (n > 2) {
				return false;
			}

			v->v[n] = res;
			++n;

			strnum.s = &ss->s[i + 1];
			strnum.len = 0;
		} else {
			++strnum.len;
		}
	}

	if (n < 2 || i != ss->len) {
		return false;
	}

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

struct version_compare_ctx {
	bool res;
	uint32_t err_node;
	const struct version *ver1;
};

static enum iteration_result
version_compare_iter(struct workspace *wk, void *_ctx, obj s2)
{
	struct version_compare_ctx *ctx = _ctx;
	struct version ver2;
	struct str ss2 = *get_str(wk, s2);
	comparator op = op_eq;

	struct {
		const struct str name;
		comparator op;
	} ops[] = {
		{ WKSTR(">="), op_ge, },
		{ WKSTR(">"),  op_gt, },
		{ WKSTR("=="), op_eq, },
		{ WKSTR("!="), op_ne, },
		{ WKSTR("<="), op_le, },
		{ WKSTR("<"),  op_lt, },
		{ WKSTR("="), op_eq, },
	};

	uint32_t i;

	for (i = 0; i < ARRAY_LEN(ops); ++i) {
		if (wk_str_startswith(&ss2, &ops[i].name)) {
			op = ops[i].op;
			ss2.s += ops[i].name.len;
			ss2.len -= ops[i].name.len;
			break;
		}
	}

	if (!string_to_version(wk, &ver2, &ss2)) {
		interp_error(wk, ctx->err_node, "invalid comparison string: %o", s2);
		return ir_err;
	}

	for (i = 0; i < 3; ++i) {
		if (ctx->ver1->v[i] != ver2.v[i]) {
			ctx->res = op(ctx->ver1->v[i], ver2.v[i]);

			if (!ctx->res) {
				return ir_done;
			}

			return ir_cont;
		}
	}

	if (op == op_eq || op == op_ge || op == op_le) {
		ctx->res = true;
		return ir_cont;
	}

	ctx->res = false;
	return ir_done;
}

bool
version_compare(struct workspace *wk, uint32_t err_node, const struct version *ver1, obj arr, bool *res)
{
	struct version_compare_ctx ctx = {
		.err_node = err_node,
		.ver1 = ver1,
	};

	if (!obj_array_foreach(wk, arr, &ctx, version_compare_iter)) {
		return false;
	}

	*res = ctx.res;
	return true;
}

static bool
func_version_compare(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	struct version v;
	if (!string_to_version(wk, &v, get_str(wk, rcvr))) {
		interp_error(wk, args_node, "comparing against invalid version string: %o", rcvr);
		return false;
	}

	bool *comp_res = &make_obj(wk, res, obj_bool)->dat.boolean;

	struct version_compare_ctx ctx = {
		.err_node = an[0].node,
		.ver1 = &v,
	};

	if (version_compare_iter(wk, &ctx, an[0].val) == ir_err) {
		return false;
	}

	*comp_res = ctx.res;
	return true;
}

static bool
func_string_to_int(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const struct str *ss = get_str(wk, rcvr);

	char *endptr = NULL;
	int64_t n = strtol(ss->s, &endptr, 10);
	if (endptr - ss->s != ss->len) {
		interp_error(wk, args_node, "unable to parse %o", rcvr);
		return false;
	}

	make_obj(wk, obj, obj_number)->dat.num = n;
	return true;
}

static bool
func_string_startswith(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = wk_str_startswith(get_str(wk, rcvr), get_str(wk, an[0].val));
	return true;
}

static bool
func_string_endswith(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = wk_str_endswith(get_str(wk, rcvr), get_str(wk, an[0].val));
	return true;
}

const struct func_impl_name impl_tbl_string[] = {
	{ "format", func_format },
	{ "join", func_join },
	{ "split", func_split },
	{ "startswith", func_string_startswith },
	{ "endswith", func_string_endswith },
	{ "strip", func_strip },
	{ "to_int", func_string_to_int },
	{ "to_upper", func_to_upper },
	{ "to_lower", func_to_lower },
	{ "underscorify", func_underscorify },
	{ "version_compare", func_version_compare },
	{ NULL, NULL },
};
