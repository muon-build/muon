/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Harley Swick <fancycade@mycanofbeans.com>
 * SPDX-FileCopyrightText: Eli Schwartz <eschwartz@archlinux.org>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "coerce.h"
#include "error.h"
#include "functions/string.h"
#include "lang/func_lookup.h"
#include "lang/lexer.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "platform/assert.h"
#include "rpmvercmp.h"
#include "util.h"

static bool
func_strip(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = str_strip(wk, get_str(wk, self), an[0].set ? get_str(wk, an[0].val) : NULL, 0);
	return true;
}

static bool
func_to_upper(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = str_clone_mutable(wk, self);
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
func_to_lower(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = str_clone_mutable(wk, self);

	const struct str *ss = get_str(wk, *res);

	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if ('A' <= ss->s[i] && ss->s[i] <= 'Z') {
			((char *)ss->s)[i] += 32;
		}
	}

	return true;
}

bool
string_format(struct workspace *wk, uint32_t err_node, obj str, obj *res, void *ctx, string_format_cb cb)
{
	const struct str *ss_in = get_str(wk, str);
	struct str key, text = { .s = ss_in->s };
	obj elem;

	uint32_t i;
	bool reading_id = false;

	*res = make_str(wk, "");

	for (i = 0; i < ss_in->len; ++i) {
		if (reading_id) {
			key.len = &ss_in->s[i] - key.s;

			if (ss_in->s[i] == '@') {
				switch (cb(wk, err_node, ctx, &key, &elem)) {
				case format_cb_not_found: {
					vm_error(wk, "key '%.*s' not found", key.len, key.s);
					return false;
				}
				case format_cb_error: return false;
				case format_cb_found: {
					obj coerced;
					if (!coerce_string(wk, err_node, elem, &coerced)) {
						return false;
					}

					str_apps(wk, res, coerced);
					text.s = &ss_in->s[i + 1];
					break;
				}
				case format_cb_skip: {
					str_appn(wk, res, key.s - 1, key.len + 1);
					text.s = &ss_in->s[i];
					--i;
					break;
				}
				}

				reading_id = false;
			} else if (!is_valid_inside_of_identifier(ss_in->s[i])) {
				str_appn(wk, res, key.s - 1, key.len + 1);
				text.s = &ss_in->s[i];
				reading_id = false;
			}
		} else if (ss_in->s[i] == '@' && is_valid_inside_of_identifier(ss_in->s[i + 1])) {
			text.len = &ss_in->s[i] - text.s;
			str_appn(wk, res, text.s, text.len);
			text.s = &ss_in->s[i];

			reading_id = true;
			key.s = &ss_in->s[i + 1];
		} else if (ss_in->s[i] == '\\' && ss_in->s[i + 1] == '@') {
			text.len = &ss_in->s[i] - text.s;
			str_appn(wk, res, text.s, text.len);
			text.s = &ss_in->s[i + 1];
			++i;
		}
	}

	text.len = &ss_in->s[i] - text.s;
	str_appn(wk, res, text.s, text.len);

	if (reading_id) {
		vm_warning(wk, "unclosed @");
	}

	return true;
}

struct func_format_ctx {
	obj arr;
};

static enum format_cb_result
func_format_cb(struct workspace *wk, uint32_t node, void *_ctx, const struct str *key, uint32_t *elem)
{
	struct func_format_ctx *ctx = _ctx;
	int64_t i;

	if (!str_to_i(key, &i, false)) {
		return format_cb_skip;
	}

	if (!boundscheck(wk, node, get_obj_array(wk, ctx->arr)->len, &i)) {
		return format_cb_error;
	}

	*elem = obj_array_index(wk, ctx->arr, i);

	return format_cb_found;
}

static bool
func_format(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_message }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	struct func_format_ctx ctx = {
		.arr = an[0].val,
	};

	obj str;
	if (!string_format(wk, an[0].node, self, &str, &ctx, func_format_cb)) {
		return false;
	}

	*res = str;
	return true;
}

static bool
func_underscorify(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = str_clone_mutable(wk, self);

	const struct str *ss = get_str(wk, *res);

	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if (!(('a' <= ss->s[i] && ss->s[i] <= 'z') || ('A' <= ss->s[i] && ss->s[i] <= 'Z')
			    || ('0' <= ss->s[i] && ss->s[i] <= '9'))) {
			((char *)ss->s)[i] = '_';
		}
	}

	return true;
}

static bool
func_split(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string, .optional = true }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const struct str *split = an[0].set ? get_str(wk, an[0].val) : NULL, *ss = get_str(wk, self);

	*res = str_split(wk, ss, split);
	return true;
}

static bool
func_splitlines(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	*res = str_splitlines(wk, get_str(wk, self));
	return true;
}

static bool
func_join(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | obj_string }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	return obj_array_join(wk, true, an[0].val, self, res);
}

bool
version_compare(const struct str *ver1, const struct str *_ver2)
{
	struct str ver2 = *_ver2;

	enum op_type {
		op_ge,
		op_gt,
		op_eq,
		op_ne,
		op_le,
		op_lt,
	};
	enum op_type op = op_eq;

	struct {
		const struct str name;
		enum op_type op;
	} ops[] = {
		{ STR(">="), op_ge },
		{ STR(">"), op_gt },
		{ STR("=="), op_eq },
		{ STR("!="), op_ne },
		{ STR("<="), op_le },
		{ STR("<"), op_lt },
		{ STR("="), op_eq },
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(ops); ++i) {
		if (str_startswith(&ver2, &ops[i].name)) {
			op = ops[i].op;
			ver2.s += ops[i].name.len;
			ver2.len -= ops[i].name.len;
			break;
		}
	}

	int8_t cmp = rpmvercmp(ver1, &ver2);

	switch (op) {
	case op_eq: return cmp == 0; break;
	case op_ne: return cmp != 0; break;
	case op_gt: return cmp == 1; break;
	case op_ge: return cmp >= 0; break;
	case op_lt: return cmp == -1; break;
	case op_le: return cmp <= 0; break;
	default: UNREACHABLE_RETURN;
	}
}

bool
version_compare_list(struct workspace *wk, const struct str *ver, obj cmp_arr)
{
	obj o;
	obj_array_for(wk, cmp_arr, o) {
		if (!version_compare(ver, get_str(wk, o))) {
			return false;
		}
	}

	return true;
}

static bool
func_version_compare(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	bool matches = version_compare(get_str(wk, self), get_str(wk, an[0].val));

	*res = make_obj_bool(wk, matches);
	return true;
}

static bool
func_string_to_int(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	const struct str *ss = get_str(wk, self);

	int64_t n;
	if (!str_to_i(ss, &n, true)) {
		vm_error(wk, "unable to parse %o", self);
		return false;
	}

	*res = make_obj(wk, obj_number);
	set_obj_number(wk, *res, n);
	return true;
}

static bool
func_string_startswith(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, str_startswith(get_str(wk, self), get_str(wk, an[0].val)));
	return true;
}

static bool
func_string_endswith(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, str_endswith(get_str(wk, self), get_str(wk, an[0].val)));
	return true;
}

static bool
func_string_substring(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_number, .optional = true }, { obj_number, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const struct str *s = get_str(wk, self);
	int64_t start = 0, end = s->len;

	if (an[0].set) {
		start = get_obj_number(wk, an[0].val);
	}

	if (an[1].set) {
		end = get_obj_number(wk, an[1].val);
	}

	if (start < 0) {
		start = s->len + start;
	}

	if (end < 0) {
		end = s->len + end;
	}

	if (end < start) {
		end = start;
	}

	start = MAX(0, start);

	if (start > end || start > s->len) {
		*res = make_str(wk, "");
		return true;
	}

	*res = make_strn(wk, &s->s[start], MIN(end - start, s->len - start));
	return true;
}

static bool
func_string_replace(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const struct str *s = get_str(wk, self);
	const struct str *find = get_str(wk, an[0].val);
	const struct str *replace = get_str(wk, an[1].val);
	struct str tmp, pre = {
		.s = s->s,
		.len = 0,
	};

	*res = make_str(wk, "");

	uint32_t i;
	for (i = 0; i < s->len; ++i) {
		tmp = (struct str){
			.s = &s->s[i],
			.len = s->len - i,
		};

		if (str_startswith(&tmp, find)) {
			str_appn(wk, res, pre.s, pre.len);
			str_appn(wk, res, replace->s, replace->len);
			i += find->len;
			pre.s = &s->s[i];
			pre.len = 0;

			--i;
		} else {
			++pre.len;
		}
	}

	str_appn(wk, res, pre.s, pre.len);

	return true;
}

static bool
func_string_contains(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const struct str *s = get_str(wk, self);
	const struct str *find = get_str(wk, an[0].val);
	struct str tmp;

	bool found = false;
	uint32_t i;
	for (i = 0; i < s->len; ++i) {
		tmp = (struct str){
			.s = &s->s[i],
			.len = s->len - i,
		};

		if (str_startswith(&tmp, find)) {
			found = true;
			break;
		}
	}

	*res = make_obj_bool(wk, found);
	return true;
}

const struct func_impl impl_tbl_string[] = {
	{ "contains", func_string_contains, tc_bool, true },
	{ "endswith", func_string_endswith, tc_bool, true },
	{ "format", func_format, tc_string, true },
	{ "join", func_join, tc_string, true },
	{ "replace", func_string_replace, tc_string, true },
	{ "split", func_split, tc_array, true },
	{ "splitlines", func_splitlines, tc_array, true },
	{ "startswith", func_string_startswith, tc_bool, true },
	{ "strip", func_strip, tc_string, true },
	{ "substring", func_string_substring, tc_string, true },
	{ "to_int", func_string_to_int, tc_number, true },
	{ "to_lower", func_to_lower, tc_string, true },
	{ "to_upper", func_to_upper, tc_string, true },
	{ "underscorify", func_underscorify, tc_string, true },
	{ "version_compare", func_version_compare, tc_bool, true },
	{ NULL, NULL },
};

static bool
func_string_length(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	*res = make_number(wk, get_str(wk, self)->len);
	return true;
}

static bool
string_shell_common(struct workspace *wk, enum shell_type *shell)
{
	if (vm_enum(wk, shell_type)) {
		vm_enum_value_prefixed(wk, shell_type, posix);
		vm_enum_value_prefixed(wk, shell_type, cmd);
	}

	enum kwargs { kw_shell };
	struct args_kw akw[] = { [kw_shell] = { "shell", complex_type_preset_get(wk, tc_cx_enum_shell) }, 0 };
	if (!pop_args(wk, 0, akw)) {
		return false;
	}

	*shell = shell_type_posix;
	if (akw[kw_shell].set && !vm_obj_to_enum(wk, shell_type, akw[kw_shell].val, shell)) {
		return false;
	}

	return true;
}

static bool
func_string_shell_split(struct workspace *wk, obj self, obj *res)
{
	enum shell_type shell;
	if (!string_shell_common(wk, &shell)) {
		return false;
	}

	*res = str_shell_split(wk, get_str(wk, self), shell);
	return true;
}

static bool
func_string_shell_quote(struct workspace *wk, obj self, obj *res)
{
	enum shell_type shell;
	if (!string_shell_common(wk, &shell)) {
		return false;
	}

	void (*escape_func)(struct workspace *wk, struct tstr *sb, const char *str) = 0;

	switch (shell) {
	case shell_type_posix: escape_func = shell_escape; break;
	case shell_type_cmd: escape_func = shell_escape_cmd; break;
	}

	TSTR(buf);
	escape_func(wk, &buf, get_str(wk, self)->s);
	*res = tstr_into_str(wk, &buf);
	return true;
}

const struct func_impl impl_tbl_string_internal[] = {
	{ "contains", func_string_contains, tc_bool, true },
	{ "endswith", func_string_endswith, tc_bool, true },
	{ "format", func_format, tc_string, true },
	{ "join", func_join, tc_string, true },
	{ "replace", func_string_replace, tc_string, true },
	{ "split", func_split, tc_array, true },
	{ "splitlines", func_splitlines, tc_array, true },
	{ "startswith", func_string_startswith, tc_bool, true },
	{ "strip", func_strip, tc_string, true },
	{ "substring", func_string_substring, tc_string, true },
	{ "to_int", func_string_to_int, tc_number, true },
	{ "to_lower", func_to_lower, tc_string, true },
	{ "to_upper", func_to_upper, tc_string, true },
	{ "underscorify", func_underscorify, tc_string, true },
	{ "version_compare", func_version_compare, tc_bool, true },
	{ "length", func_string_length, tc_number, true },
	{ "shell_split", func_string_shell_split, tc_array, true },
	{ "shell_quote", func_string_shell_quote, tc_string, true },
	{ NULL, NULL },
};
