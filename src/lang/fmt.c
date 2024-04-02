/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Ailin Nemui <ailin@d5421s.localdomain>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "formats/editorconfig.h"
#include "formats/ini.h"
#include "lang/fmt.h"
#include "lang/object_iterators.h"
#include "lang/string.h"
#include "platform/mem.h"

enum fmt_frag_flag {
	fmt_frag_flag_comment = 1 << 0,
	fmt_frag_flag_comment_trailing = 1 << 1,
	fmt_frag_flag_has_comment_trailing = 1 << 2,
};

struct fmt_frag {
	obj str;
	bool force_ml;
	const char *enclosing;

	struct fmt_frag *next, *child, *ws;
	uint32_t len, flags;
	bool measured;
};

struct fmt_ctx {
	struct workspace *wk;
	struct sbuf *out_buf;
	uint32_t indent, enclosing;
	bool trailing_comment;

	struct bucket_arr frags;

	/* options */
	bool space_array, kwa_ml, wide_colon, no_single_comma_function;
	uint32_t max_line_len;
	const char *indent_by;
};

static struct fmt_frag *
fmt_frag(struct fmt_ctx *f)
{
	return bucket_arr_push(&f->frags, &(struct fmt_frag){ .str = make_str(f->wk, "") });
}

static struct fmt_frag *
fmt_frag_s(struct fmt_ctx *f, const char *s)
{
	return bucket_arr_push(&f->frags, &(struct fmt_frag){ .str = make_str(f->wk, s) });
}

static struct fmt_frag *
fmt_frag_o(struct fmt_ctx *f, obj s)
{
	return bucket_arr_push(&f->frags, &(struct fmt_frag){ .str = s });
}

static struct fmt_frag *
fmt_frag_sibling(struct fmt_ctx *f, struct fmt_frag *fr, struct fmt_frag *sibling)
{
	for (; fr->next; fr = fr->next) {
	}

	fr->next = sibling;
	return sibling;
}

static struct fmt_frag *
fmt_frag_child(struct fmt_ctx *f, struct fmt_frag *parent, struct fmt_frag *child)
{
	if (!parent->child) {
		parent->child = child;
		return child;
	}

	return fmt_frag_sibling(f, parent->child, child);
}

/*******************************************************************************
 * fmt_write
 ******************************************************************************/

static void
fmt_write_indent(struct fmt_ctx *f)
{
	uint32_t i;
	for (i = 0; i < f->indent; ++i) {
		sbuf_pushs(0, f->out_buf, f->indent_by);
	}
}

static void
fmt_write_nl_indent(struct fmt_ctx *f)
{
	sbuf_pushs(0, f->out_buf, "\n");
	fmt_write_indent(f);
}

static uint32_t
saturating_add(uint32_t a, uint32_t b)
{
	uint64_t s64 = (uint64_t)a + (uint64_t)b;
	if (s64 > UINT32_MAX) {
		s64 = UINT32_MAX;
	}

	return s64;
}

static uint32_t
fmt_measure_frag_ws(struct fmt_ctx *f, struct fmt_frag *p)
{
	/* struct fmt_frag *fr; */
	/* uint32_t len; */

	if (!p->ws) {
		return 0;
	}

	return f->max_line_len;
	/* for (fr = p->ws; fr; fr = fr->next) { */
	/* } */
}

static uint32_t
fmt_measure_frag_set(struct fmt_ctx *f, struct fmt_frag *p)
{
	const struct str *str;
	struct fmt_frag *fr;
	uint32_t len = 0;

	if (p->child) {
		len = saturating_add(len, 2);
	}

	for (fr = p->child; fr; fr = fr->next) {
		fr->len = 0;

		if (fr->ws) {
			fr->len = saturating_add(fr->len, fmt_measure_frag_ws(f, fr));
		}

		if (fr->child) {
			fr->len = saturating_add(fr->len, fmt_measure_frag_set(f, fr));
		}

		str = get_str(f->wk, fr->str);
		if (strchr(str->s, '\n')) {
			// force ml if str has \n
			fr->len = saturating_add(fr->len, f->max_line_len);
		} else {
			fr->len = saturating_add(fr->len, str->len);
		}
		if (fr->next) {
			fr->len = saturating_add(fr->len, 1);
		}

		if (fr->force_ml) {
			fr->len = saturating_add(fr->len, f->max_line_len);
		}

		L("child len: %d", fr->len);
		len = saturating_add(len, fr->len);
	}

	p->len = len;
	L("ttl len: %d", p->len);
	return len;
}

static void
fmt_write_frag_trailing_comment(struct fmt_ctx *f, struct fmt_frag *p)
{
	const struct str *str;
	struct fmt_frag *fr;
	for (fr = p->ws; fr; fr = fr->next) {
		if (!(fr->flags & fmt_frag_flag_comment_trailing)) {
			continue;
		}
		str = get_str(f->wk, fr->str);
		/* if (str_eql(str, &WKSTR("\n"))) { */
		/* 	/1* fmt_write_nl_indent(f); *1/ */
		/* } else { */
		sbuf_pushn(0, f->out_buf, str->s, str->len);
		/* } */
	}
}

static void
fmt_write_frag_ws(struct fmt_ctx *f, struct fmt_frag *p)
{
	const struct str *str;
	struct fmt_frag *fr;
	for (fr = p->ws; fr; fr = fr->next) {
		if (fr->flags & fmt_frag_flag_comment_trailing) {
			continue;
		}

		if (fr->flags & fmt_frag_flag_comment) {
			str = get_str(f->wk, fr->str);
			sbuf_pushn(0, f->out_buf, str->s, str->len);
			fmt_write_nl_indent(f);
		}
	}
}

static void
fmt_write_frag_set(struct fmt_ctx *f, struct fmt_frag *p)
{
	const struct str *str;
	struct fmt_frag *fr;
	uint32_t base_len = strlen(f->indent_by) * f->indent;
	bool ml = base_len + p->len > f->max_line_len, sub_ml;

	/* L("formatting: %d, %d, %d", base_len, p->len, f->max_line_len); */

	if (p->enclosing) {
		sbuf_push(0, f->out_buf, p->enclosing[0]);
		++f->enclosing;

		sub_ml = base_len + p->len > f->max_line_len;

		if (sub_ml) {
			++f->indent;
			fmt_write_nl_indent(f);
		}
	}

	if (p->child && p->child->flags & fmt_frag_flag_has_comment_trailing) {
		fmt_write_frag_trailing_comment(f, p->child);
		fmt_write_nl_indent(f);
	}

	for (fr = p->child; fr; fr = fr->next) {
		if (fr->ws) {
			fmt_write_frag_ws(f, fr);
		}

		if (fr->child) {
			fmt_write_frag_ws(f, fr);
		}

		if (fr->child) {
			fmt_write_frag_set(f, fr);
		}

		str = get_str(f->wk, fr->str);
		sbuf_pushn(0, f->out_buf, str->s, str->len);

		if (fr->next) {
			if (fr->next->flags & fmt_frag_flag_has_comment_trailing) {
				sbuf_push(0, f->out_buf, ' ');
				fmt_write_frag_trailing_comment(f, fr->next);
			}

			if (ml) {
				fmt_write_nl_indent(f);
			} else {
				sbuf_push(0, f->out_buf, ' ');
			}
		}
	}

	if (p->enclosing) {
		if (ml) {
			--f->indent;
			fmt_write_nl_indent(f);
		}

		sbuf_push(0, f->out_buf, p->enclosing[1]);
		--f->enclosing;
	}
}

static void
fmt_write_frag_set_dbg(struct fmt_ctx *f, const struct fmt_frag *p, uint32_t d)
{
#define IND()                              \
	for (uint32_t i = 0; i < d; ++i) { \
		log_plain("  ");           \
	}

	if (p->enclosing) {
		IND();
		log_plain("%c\n", p->enclosing[0]);
	}

	struct fmt_frag *fr;

	for (fr = p->child; fr; fr = fr->next) {
		if (fr->child) {
			fmt_write_frag_set_dbg(f, fr, d + 1);
		}

		IND();
		obj_fprintf(f->wk, log_file(), "%o\n", fr->str);
		/* str = get_str(f->wk, fr->str); */
		/* if (strchr(str->s, '\n')) { */
		/* 	// force ml if str has \n */
		/* 	fr->len = saturating_add(fr->len, f->max_line_len); */
		/* } else { */
		/* 	fr->len = saturating_add(fr->len, str->len); */
		/* } */
	}

	if (p->enclosing) {
		IND();
		log_plain("%c\n", p->enclosing[1]);
	}
#undef IND
}

static void
fmt_write_line(struct fmt_ctx *f, struct fmt_frag *root)
{
	fmt_measure_frag_set(f, root);
	L("---");
	fmt_write_frag_set_dbg(f, root, 0);
	L("---");
	fmt_write_frag_set(f, root);
	fmt_write_nl_indent(f);
}

/*******************************************************************************
 * formatter
 ******************************************************************************/

static void fmt_block(struct fmt_ctx *f, struct node *n);

static void
fmt_node_ws(struct fmt_ctx *f, struct node *n, struct fmt_frag *fr)
{
	const struct str *s = get_str(f->wk, n->fmt.ws);
	struct fmt_frag *child;
	bool trailing_comment = true;
	uint32_t i, cs, ce;
	for (i = 0; i < s->len; ++i) {
		if (s->s[i] == '\n') {
			trailing_comment = false;
			fmt_frag_child(f, fr, fmt_frag_s(f, "\n"));
			/* str_appn(f->wk, &fr->str, "\n", 1); */
		} else if (s->s[i] == '#') {
			cs = i;
			for (; s->s[i] != '\n' && i < s->len; ++i) {
			}
			ce = i;

			child = fmt_frag_child(f, fr, fmt_frag_o(f, make_strn(f->wk, &s->s[cs], ce - cs)));
			child->flags |= fmt_frag_flag_comment;
			if (trailing_comment) {
				child->flags = fmt_frag_flag_comment_trailing;
				fr->flags |= fmt_frag_flag_has_comment_trailing;
			}
			/* str_appn(f->wk, &fr->str, &s->s[cs], (ce - cs) + 1); */

			L("got comment '%.*s' %s", ce - cs, &s->s[cs], trailing_comment ? "trailing" : "");
			trailing_comment = false;
		}
	}
}

static struct fmt_frag *
fmt_node(struct fmt_ctx *f, struct node *n)
{
	assert(n->type != node_type_stmt);
	struct fmt_frag *fr, *child;

	fr = fmt_frag(f);

	if (n->fmt.ws) {
		fmt_node_ws(f, n, fr);
		fr->ws = fr->child;
		fr->child = 0;
	}

	/* L("formatting %s", node_to_s(f->wk, n)); */

	switch (n->type) {
	case node_type_stmt: UNREACHABLE;

	case node_type_id_lit:
	case node_type_args:
	case node_type_def_args:
	case node_type_kw:
	case node_type_list:
	case node_type_foreach_args:
		// Skipped
		break;

	case node_type_stringify: break;
	case node_type_index: break;
	case node_type_negate: break;
	case node_type_add: break;
	case node_type_sub: break;
	case node_type_mul: break;
	case node_type_div: break;
	case node_type_mod: break;
	case node_type_not: break;
	case node_type_eq: break;
	case node_type_neq: break;
	case node_type_in: break;
	case node_type_not_in: break;
	case node_type_lt: break;
	case node_type_gt: break;
	case node_type_leq: break;
	case node_type_geq: break;
	case node_type_id: break;
	case node_type_string:
	case node_type_number: {
		str_apps(f->wk, &fr->str, n->data.str);
		break;
	}
	case node_type_bool: {
		str_app(f->wk, &fr->str, n->data.num ? "true" : "false");
		break;
	}
	case node_type_array: {
		fr->enclosing = "[]";

		while (true) {
			if (n->l) {
				child = fmt_frag_child(f, fr, fmt_node(f, n->l));
			}
			if (!n->r) {
				break;
			}

			str_app(f->wk, &child->str, ",");
			n = n->r;

			if (!n->r && !n->l) {
				// trailing comma
				fr->force_ml = true;
			}
		}
		break;
	}
	case node_type_dict: break;
	case node_type_assign: break;
	case node_type_plusassign: break;
	case node_type_method: {
		break;
	}
	case node_type_call: {
		break;
	}
	case node_type_return: {
		break;
	}
	case node_type_foreach: {
		break;
	}
	case node_type_continue: {
		break;
	}
	case node_type_break: {
		break;
	}
	case node_type_if: {
		break;
	}
	case node_type_ternary: {
		break;
	}
	case node_type_or: {
		break;
	}
	case node_type_and: {
		break;
	}
	case node_type_func_def: {
		break;
	}
	}

	return fr;
}

static void
fmt_block(struct fmt_ctx *f, struct node *n)
{
	struct fmt_frag *line;

	while (n) {
		assert(n->type == node_type_stmt);

		if (!n->l) {
			fmt_write_nl_indent(f);
			goto cont;
		}

		line = fmt_frag(f);
		fmt_frag_child(f, line, fmt_node(f, n->l));

		fmt_write_line(f, line);

		bucket_arr_clear(&f->frags);

cont:
		n = n->r;
	}
}

/*******************************************************************************
 * config parsing
 ******************************************************************************/

static bool
fmt_cfg_parse_cb(void *_ctx,
	struct source *src,
	const char *sect,
	const char *k,
	const char *v,
	struct source_location location)
{
	struct fmt_ctx *ctx = _ctx;

	enum val_type {
		type_uint,
		type_str,
		type_bool,
	};

	static const struct {
		const char *name;
		enum val_type type;
		uint32_t off;
	} keys[] = { { "max_line_len", type_uint, offsetof(struct fmt_ctx, max_line_len) },
		{ "indent_by", type_str, offsetof(struct fmt_ctx, indent_by) },
		{ "space_array", type_bool, offsetof(struct fmt_ctx, space_array) },
		{ "kwargs_force_multiline", type_bool, offsetof(struct fmt_ctx, kwa_ml) },
		{ "kwa_ml", type_bool, offsetof(struct fmt_ctx, kwa_ml) }, // kept for backwards compat
		{ "wide_colon", type_bool, offsetof(struct fmt_ctx, wide_colon) },
		{ "no_single_comma_function", type_bool, offsetof(struct fmt_ctx, no_single_comma_function) },
		0 };

	if (!k || !*k) {
		error_messagef(src, location, log_error, "missing key");
		return false;
	} else if (!v || !*v) {
		error_messagef(src, location, log_error, "missing value");
		return false;
	} else if (sect) {
		error_messagef(src, location, log_error, "invalid section");
		return false;
	}

	uint32_t i;
	for (i = 0; keys[i].name; ++i) {
		if (strcmp(k, keys[i].name) == 0) {
			void *val_dest = ((uint8_t *)ctx + keys[i].off);

			switch (keys[i].type) {
			case type_uint: {
				char *endptr = NULL;
				long long lval = strtoll(v, &endptr, 10);
				if (*endptr) {
					error_messagef(src, location, log_error, "unable to parse integer");
					return false;
				} else if (lval < 0 || lval > (long long)UINT32_MAX) {
					error_messagef(
						src, location, log_error, "integer outside of range 0-%u", UINT32_MAX);
					return false;
				}

				uint32_t val = lval;
				memcpy(val_dest, &val, sizeof(uint32_t));
				break;
			}
			case type_str: {
				char *start, *end;
				start = strchr(v, '\'');
				end = strrchr(v, '\'');

				if (!start || !end || start == end) {
					error_messagef(src, location, log_error, "expected single-quoted string");
					return false;
				}

				*end = 0;
				++start;

				memcpy(val_dest, &start, sizeof(char *));
				break;
			}
			case type_bool: {
				bool val;
				if (strcmp(v, "true") == 0) {
					val = true;
				} else if (strcmp(v, "false") == 0) {
					val = false;
				} else {
					error_messagef(src,
						location,
						log_error,
						"invalid value for bool, expected true/false");
					return false;
				}

				memcpy(val_dest, &val, sizeof(bool));
				break;
			}
			}
		}
	}

	return true;
}

/*******************************************************************************
 * entrypoint
 ******************************************************************************/

bool
fmt(struct source *src, FILE *out, const char *cfg_path, bool check_only, bool editorconfig)
{
	bool ret = false;
	struct sbuf out_buf;
	struct workspace wk = { 0 };
	workspace_init_bare(&wk);
	struct fmt_ctx f = {
		.wk = &wk,
		.out_buf = &out_buf,
		.max_line_len = 80,
		.indent_by = "    ",
		.space_array = false,
		.kwa_ml = false,
		.wide_colon = false,
		.no_single_comma_function = false,
	};

	bucket_arr_init(&f.frags, 1024, sizeof(struct fmt_frag));

	if (check_only) {
		sbuf_init(&out_buf, NULL, 0, sbuf_flag_overflow_alloc);
	} else {
		out_buf.flags = sbuf_flag_write;
		out_buf.buf = (void *)out;
	}

	if (editorconfig) {
		struct editorconfig_opts editorconfig_opts;
		try_parse_editorconfig(src, &editorconfig_opts);
		if (editorconfig_opts.indent_by) {
			f.indent_by = editorconfig_opts.indent_by;
		}
	}

	char *cfg_buf = NULL;
	struct source cfg_src = { 0 };
	if (cfg_path) {
		if (!fs_read_entire_file(cfg_path, &cfg_src)) {
			goto ret;
		} else if (!ini_parse(cfg_path, &cfg_src, &cfg_buf, fmt_cfg_parse_cb, &f)) {
			goto ret;
		}
	}

	enum vm_compile_mode compile_mode = vm_compile_mode_fmt;
	if (str_endswith(&WKSTR(src->label), &WKSTR(".meson"))) {
		compile_mode |= vm_compile_mode_language_extended;
	}

	struct node *n;
	if (!(n = parse_fmt(&wk, src, compile_mode))) {
		goto ret;
	}

	fmt_block(&f, n);

	if (check_only) {
		if (src->len != out_buf.len) {
			goto ret;
		} else if (memcmp(src->src, out_buf.buf, src->len)) {
			goto ret;
		}
	}

	ret = true;
ret:
	workspace_destroy_bare(&wk);
	if (cfg_buf) {
		z_free(cfg_buf);
	}
	sbuf_destroy(&out_buf);
	fs_source_destroy(&cfg_src);
	return ret;
}
