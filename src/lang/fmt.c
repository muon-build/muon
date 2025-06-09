/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Ailin Nemui <ailin@d5421s.localdomain>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "error.h"
#include "formats/editorconfig.h"
#include "formats/ini.h"
#include "lang/fmt.h"
#include "lang/object_iterators.h"
#include "lang/parser.h"
#include "lang/string.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/mem.h"

enum fmt_frag_flag {
	fmt_frag_flag_add_trailing_comma = 1 << 1,
	fmt_frag_flag_enclosing_space = 1 << 2,
	fmt_frag_flag_has_comment_trailing = 1 << 3,
	fmt_frag_flag_fmt_off = 1 << 4,
	fmt_frag_flag_fmt_on = 1 << 5,
	fmt_frag_flag_stick_left = 1 << 6,
	fmt_frag_flag_stick_left_unless_enclosed = 1 << 7,
	fmt_frag_flag_stick_right = 1 << 8,
	fmt_frag_flag_stick_line_left = 1 << 9,
	fmt_frag_flag_stick_line_right = 1 << 10,
	fmt_frag_flag_stick_line_left_unless_enclosed = 1 << 11,
	fmt_frag_flag_force_single_line = 1 << 12,
	fmt_frag_flag_enclosed_extra_indent = 1 << 13,
};

enum fmt_frag_type {
	fmt_frag_type_expr,
	fmt_frag_type_line,
	fmt_frag_type_block,
	fmt_frag_type_lines, // if returned from fmt_node, will be merged into current block
	fmt_frag_type_ws_newline,
	fmt_frag_type_ws_comment,
	fmt_frag_type_ws_comment_trailing,
};

struct fmt_frag {
	obj str;
	enum fmt_frag_type type;
	enum node_type node_type;
	bool force_ml; // TODO: should this be a flag?
	const char *enclosing;

	struct fmt_frag *next, *child, *pre_ws, *post_ws;
	uint32_t flags;
};

struct fmt_out_block {
	obj str;
	bool raw;
};

struct fmt_ctx {
	struct workspace *wk;
	struct tstr *out_buf;
	obj raw_blocks;
	uint32_t indent, enclosing, raw_block_idx, measured_len;
	bool trailing_comment, fmt_on, measuring, line_has_content;

	struct bucket_arr frags;

	struct arr out_blocks;
	struct arr list_tmp;
	struct fmt_opts opts;
};

static struct fmt_frag *
fmt_frag(struct fmt_ctx *f, enum fmt_frag_type type)
{
	return bucket_arr_push(&f->frags, &(struct fmt_frag){ .type = type });
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
fmt_frag_sibling(struct fmt_frag *fr, struct fmt_frag *sibling)
{
	for (; fr->next; fr = fr->next) {
	}

	fr->next = sibling;
	return sibling;
}

static struct fmt_frag *
fmt_frag_child(struct fmt_frag **dest, struct fmt_frag *child)
{
	if (!*dest) {
		return *dest = child;
	}

	return fmt_frag_sibling(*dest, child);
}

static struct fmt_frag *
fmt_frag_last_child(struct fmt_frag *fr)
{
	for (fr = fr->child; fr->next; fr = fr->next) {
	}

	return fr;
}

static void
fmt_frag_broadcast_flag(struct fmt_frag *fr, enum fmt_frag_flag flag)
{
	for (; fr; fr = fr->next) {
		fr->flags |= flag;
	}
}

static obj
fmt_obj_as_simple_str(struct fmt_ctx *f, obj s)
{
	struct node *n_str_stmt, *n_str;
	const struct str *str = get_str(f->wk, s);

	n_str_stmt = parse(f->wk, &(struct source){ .src = str->s, .len = str->len }, 0);

	assert(n_str_stmt && n_str_stmt->type == node_type_stmt && n_str_stmt->l);
	n_str = n_str_stmt->l;

	if (n_str->type != node_type_string) {
		return 0;
	}

	return n_str->data.str;
}

static obj
fmt_frag_as_simple_str(struct fmt_ctx *f, const struct fmt_frag *fr)
{
	if (!fr || fr->node_type != node_type_string) {
		return 0;
	}

	return fmt_obj_as_simple_str(f, fr->str);
}

static void
fmt_frag_move_ws(struct fmt_frag *dst, struct fmt_frag *src)
{
	uint32_t flag_mask = fmt_frag_flag_has_comment_trailing, flags;

	flags = src->flags & flag_mask;
	src->flags &= ~flags;
	dst->flags |= flags;

	dst->pre_ws = src->pre_ws;
	dst->post_ws = src->post_ws;

	src->pre_ws = 0;
	src->post_ws = 0;
}

/*******************************************************************************
 * debug printing
 ******************************************************************************/

struct tree_indent {
	uint32_t indent, bars;
	uint32_t i, len;
};

static void
tree_indent_print(const struct tree_indent *ti)
{
	uint32_t i;
	for (i = 0; i < ti->indent; ++i) {
		if (i < ti->indent - 1) {
			if (ti->bars & (1 << i)) {
				log_raw("│   ");
			} else {
				log_raw("    ");
			}
		} else if (!ti->len || ti->i == ti->len - 1) {
			log_raw("└── ");
		} else {
			log_raw("├── ");
		}
	}
}

static uint32_t
fmt_frag_dbg_len(const struct fmt_frag *p)
{
	uint32_t len = 0;
	struct fmt_frag *fr;
	for (fr = p->pre_ws; fr; fr = fr->next) {
		++len;
	}
	for (fr = p->child; fr; fr = fr->next) {
		++len;
	}
	for (fr = p->post_ws; fr; fr = fr->next) {
		++len;
	}
	return len;
}

static void
fmt_write_frag_set_dbg_ws(struct fmt_ctx *f, const struct fmt_frag *pws, struct tree_indent *sub_ti, const char *label)
{
	const struct fmt_frag *ws;

	for (ws = pws; ws; ws = ws->next) {
		tree_indent_print(sub_ti);

		log_raw("%s: ", label);

		if (ws->type == fmt_frag_type_ws_newline) {
			log_raw("newline");
		} else {
			obj_lprintf(f->wk, log_info, "# %o", ws->str);
			if (ws->type == fmt_frag_type_ws_comment) {
				log_raw(" comment");
			} else if (ws->type == fmt_frag_type_ws_comment_trailing) {
				log_raw(" comment_trailing");
			} else {
				UNREACHABLE;
			}
		}
		log_raw("\n");
		++sub_ti->i;
	}
}

static uint32_t fmt_measure_frag(struct fmt_ctx *f, struct fmt_frag *p);

static void
fmt_write_frag_set_dbg(struct fmt_ctx *f, struct fmt_frag *p, const struct tree_indent *ti, const struct fmt_frag *hl)
{
	if (!log_should_print(log_debug)) {
		return;
	}

	struct fmt_frag *fr;
	struct tree_indent sub_ti;

	if (p == hl) {
		log_raw("\033[34m");
	}

	tree_indent_print(ti);

	if (p->str) {
		obj_lprintf(f->wk, log_debug, "%o", p->str);
	} else if (p->type == fmt_frag_type_block) {
		obj_lprintf(f->wk, log_debug, "block");
	} else if (p->type == fmt_frag_type_line) {
		obj_lprintf(f->wk, log_debug, "line");
	} else if (p->enclosing) {
		obj_lprintf(f->wk, log_debug, "%s", p->enclosing);
	} else {
		obj_lprintf(f->wk, log_debug, "?");
	}

	if (p->flags) {
		obj flags;
		flags = make_obj(f->wk, obj_array);

		struct {
			uint32_t flag;
			const char *label;
		} names[] = {
#define FF(v) { fmt_frag_flag_##v, #v }
			FF(add_trailing_comma),
			FF(enclosing_space),
			FF(has_comment_trailing),
			FF(fmt_off),
			FF(fmt_on),
			FF(stick_left),
			FF(stick_left_unless_enclosed),
			FF(stick_right),
			FF(stick_line_left),
			FF(stick_line_right),
			FF(stick_line_left_unless_enclosed),
			FF(force_single_line),
			FF(enclosed_extra_indent),
#undef FF
		};

		uint32_t i;
		for (i = 0; i < ARRAY_LEN(names); ++i) {
			if (p->flags & names[i].flag) {
				obj_array_push(f->wk, flags, make_str(f->wk, names[i].label));
			}
		}

		obj joined;
		obj_array_join(f->wk, false, flags, make_str(f->wk, ","), &joined);

		log_raw(" <%s>", get_cstr(f->wk, joined));
	}

	if (p == hl) {
		log_raw("\033[0m");
	}

	log_raw(" - %d", fmt_measure_frag(f, p));

	log_raw("\n");

	sub_ti = (struct tree_indent){
		.len = fmt_frag_dbg_len(p),
		.indent = ti->indent + 1,
		.bars = ti->bars,
	};
	if (ti->i < ti->len - 1) {
		sub_ti.bars |= (1 << (ti->indent - 1));
	}

	fmt_write_frag_set_dbg_ws(f, p->pre_ws, &sub_ti, "pre_ws");

	uint32_t i;
	for (fr = p->child, i = 0; fr; fr = fr->next, ++i) {
		fmt_write_frag_set_dbg(f, fr, &sub_ti, hl);

		++sub_ti.i;
	}

	fmt_write_frag_set_dbg_ws(f, p->post_ws, &sub_ti, "post_ws");
}

/*******************************************************************************
 * fmt_write
 ******************************************************************************/

static void
fmt_push_out_block(struct fmt_ctx *f)
{
	arr_push(&f->out_blocks,
		&(struct fmt_out_block){
			.str = tstr_into_str(f->wk, f->out_buf),
		});

	*f->out_buf = (struct tstr){ 0 };
	tstr_init(f->out_buf, 0, 0, 0);
}

static void
fmt_write_nl(struct fmt_ctx *f)
{
	if (f->measuring) {
		if (f->enclosing) {
			f->measured_len += f->opts.max_line_len + 1;
		}
		return;
	} else if (!f->fmt_on) {
		return;
	}

	f->line_has_content = false;
	tstr_push(f->wk, f->out_buf, '\n');
}

static void
fmt_write(struct fmt_ctx *f, const char *s, uint32_t n)
{
	if (f->measuring) {
		if (strchr(s, '\n')) {
			f->measured_len += f->opts.max_line_len + 1;
		} else {
			f->measured_len += n;
		}
		return;
	} else if (!f->fmt_on) {
		return;
	}

	if (!f->line_has_content) {
		uint32_t i, j;
		for (i = 1; i < f->indent; ++i) {
			switch (f->opts.indent_style) {
			case fmt_indent_style_space: {
				for (j = 0; j < f->opts.indent_size; ++j) {
					tstr_push(f->wk, f->out_buf, ' ');
				}
				break;
			}
			case fmt_indent_style_tab: {
				tstr_push(f->wk, f->out_buf, '\t');
				break;
			}
			}
		}
	}

	f->line_has_content = true;

	tstr_pushn(f->wk, f->out_buf, s, n);
}

static void
fmt_writestr(struct fmt_ctx *f, const struct str *s)
{
	fmt_write(f, s->s, s->len);
}

static void
fmt_writes(struct fmt_ctx *f, const char *s)
{
	fmt_write(f, s, strlen(s));
}

static void
fmt_write_frag_comment(struct fmt_ctx *f, struct fmt_frag *comment)
{
	const struct str *str = get_str(f->wk, comment->str);

	if (f->measuring) {
		f->measured_len += f->opts.max_line_len + 1;
		return;
	}

	if (comment->flags & fmt_frag_flag_fmt_on) {
		f->fmt_on = true;

		obj raw_block;
		raw_block = obj_array_index(f->wk, f->raw_blocks, f->raw_block_idx);
		arr_push(&f->out_blocks,
			&(struct fmt_out_block){
				.str = raw_block,
				.raw = true,
			});
		++f->raw_block_idx;
	}

	fmt_writes(f, "#");

	if (str->len) {
		fmt_writestr(f, str);
	}

	if (comment->flags & fmt_frag_flag_fmt_off) {
		fmt_push_out_block(f);
		f->fmt_on = false;
	}
}

static void
fmt_write_frag_trailing_comment(struct fmt_ctx *f, struct fmt_frag *ws)
{
	struct fmt_frag *fr;
	for (fr = ws; fr; fr = fr->next) {
		if (fr->type != fmt_frag_type_ws_comment_trailing) {
			continue;
		}

		if (f->line_has_content) {
			fmt_writes(f, f->opts.indent_before_comments);
		}
		fmt_write_frag_comment(f, fr);
	}
}

enum fmt_write_frag_ws_mode {
	fmt_write_frag_ws_mode_pre,
	fmt_write_frag_ws_mode_post,
	fmt_write_frag_ws_mode_empty_line,
};

static void
fmt_write_frag_ws(struct fmt_ctx *f, struct fmt_frag *ws, enum fmt_write_frag_ws_mode mode)
{
	uint32_t newlines = 0, consecutive_newlines = 0;

	struct fmt_frag *fr;
	for (fr = ws; fr; fr = fr->next) {
		switch (fr->type) {
		case fmt_frag_type_ws_comment_trailing: {
			++newlines;
			break;
		}
		case fmt_frag_type_ws_comment: {
			consecutive_newlines = 0;
			if (mode == fmt_write_frag_ws_mode_post) {
				fmt_write_nl(f);
			}

			fmt_write_frag_comment(f, fr);

			if (mode == fmt_write_frag_ws_mode_empty_line) {
				if (fr->next) {
					fmt_write_nl(f);
				}
			}

			if (mode == fmt_write_frag_ws_mode_pre) {
				fmt_write_nl(f);
			}

			++newlines;
			break;
		}
		case fmt_frag_type_ws_newline: {
			if (f->enclosing) {
				if (newlines < 1) {
					// Skip the first newline
					++newlines;
					continue;
				} else if (consecutive_newlines > 0) {
					// After that only print 1 consecutive newline
					continue;
				}
			} else {
				if (newlines < 1) {
					// Skip the first newline
					++newlines;
					continue;
				} else if (consecutive_newlines >= 1) {
					continue;
				}
			}

			fmt_write_nl(f);
			++newlines;
			++consecutive_newlines;
			break;
		}
		default: UNREACHABLE;
		}
	}
}

static void fmt_write_frag(struct fmt_ctx *f, struct fmt_frag *p);

static uint32_t
fmt_measure_frag(struct fmt_ctx *f, struct fmt_frag *p)
{
	f->measured_len = 0;
	f->measuring = true;
	fmt_write_frag(f, p);
	f->measuring = false;
	return f->measured_len;
}

static void
fmt_write_frag(struct fmt_ctx *f, struct fmt_frag *p)
{
	struct fmt_frag *fr;
	uint32_t base_len = f->opts.indent_size * f->indent;
	bool ml = f->measuring ? false : base_len + fmt_measure_frag(f, p) > f->opts.max_line_len;

	if (f->measuring) {
		if (p->force_ml) {
			f->measured_len += f->opts.max_line_len + 1;
			return;
		} else if (p->flags & fmt_frag_flag_force_single_line) {
			f->measured_len = 0;
			return;
		}
	}

	// Write preceeding whitespace
	if (p->pre_ws) {
		enum fmt_write_frag_ws_mode mode = fmt_write_frag_ws_mode_pre;

		if (p->type == fmt_frag_type_line && !p->child) {
			// Handle empty lines a bit differently
			mode = fmt_write_frag_ws_mode_empty_line;
		}

		fmt_write_frag_ws(f, p->pre_ws, mode);
	}

	// Write enclosing characters and indent if necessary
	if (p->enclosing) {
		fmt_write(f, &p->enclosing[0], 1);
		++f->enclosing;

		if (ml) {
			++f->indent;
			if (p->flags & fmt_frag_flag_enclosed_extra_indent) {
				++f->indent;
			}
		}

		if (p->child
			&& (p->child->flags & fmt_frag_flag_stick_left
				|| p->child->flags & fmt_frag_flag_stick_line_left)) {
			//
		} else {
			if (ml) {
				fmt_write_nl(f);
			} else if (p->flags & fmt_frag_flag_enclosing_space) {
				fmt_writes(f, " ");
			}
		}
	} else if (p->type == fmt_frag_type_block) {
		++f->indent;
	}

	if (p->child && p->child->pre_ws && p->child->pre_ws->flags & fmt_frag_flag_has_comment_trailing) {
		// If our first child has a preceeding trailing comment, write
		// it here
		fmt_write_frag_trailing_comment(f, p->child->pre_ws);
		fmt_write_nl(f);
	}

	// If we have children, write them out
	for (fr = p->child; fr; fr = fr->next) {
		fmt_write_frag(f, fr);

		if (fr->next) {
			// If the next child has a trailing comment, write it before the newline
			if (fr->next->pre_ws) {
				fmt_write_frag_trailing_comment(f, fr->next->pre_ws);
			}

			// Write the child separator
			if ((fr->next->flags & fmt_frag_flag_stick_left) || (fr->flags & fmt_frag_flag_stick_right)) {
			} else {
				bool stick_line
					= (fr->flags & fmt_frag_flag_stick_line_right)
					  || (fr->next->flags & fmt_frag_flag_stick_line_left)
					  || (!f->enclosing
						  && (fr->next->flags & fmt_frag_flag_stick_line_left_unless_enclosed
							  || fr->next->flags
								     & fmt_frag_flag_stick_left_unless_enclosed));

				if (ml && !stick_line) {
					fmt_write_nl(f);
				} else if (fr->next->flags & fmt_frag_flag_stick_left_unless_enclosed) {
				} else {
					fmt_writes(f, " ");
				}
			}
		}
	}

	// Write out the current fragment's string
	if (p->str) {
		fmt_writestr(f, get_str(f->wk, p->str));
	}

	// Write enclosing characters and dedent if necessary
	if (p->enclosing) {
		if (ml) {
			--f->indent;
		}

		if (p->child && (fmt_frag_last_child(p)->flags & fmt_frag_flag_stick_right)) {
			//
		} else {
			if (ml) {
				if ((p->flags & fmt_frag_flag_add_trailing_comma)) {
					fmt_writes(f, ",");
				}
				fmt_write_nl(f);
			} else if (p->flags & fmt_frag_flag_enclosing_space) {
				fmt_writes(f, " ");
			}
		}

		fmt_write(f, &p->enclosing[1], 1);
		--f->enclosing;

		if (ml && p->flags & fmt_frag_flag_enclosed_extra_indent) {
			--f->indent;
		}
	} else if (p->type == fmt_frag_type_block) {
		--f->indent;
	}

	if (p->post_ws) {
		fmt_write_frag_trailing_comment(f, p->post_ws);

		fmt_write_frag_ws(f, p->post_ws, fmt_write_frag_ws_mode_post);
	}
}

static void
fmt_write_block(struct fmt_ctx *f, struct fmt_frag *block)
{
	if (!block) {
		return;
	}

	fmt_write_frag_set_dbg(f, block, &(struct tree_indent){ .indent = 1, .len = 1 }, 0);

	fmt_write_frag(f, block);
}

/*******************************************************************************
 * formatter
 ******************************************************************************/

static void
fmt_node_ws(struct fmt_ctx *f, struct node *n, obj ws, struct fmt_frag **dest)
{
	const struct str *s = get_str(f->wk, ws);
	struct str comment;
	struct fmt_frag *child;
	bool trailing_comment = true;
	uint32_t i, cs, ce;
	for (i = 0; i < s->len; ++i) {
		if (s->s[i] == '\n') {
			trailing_comment = false;
			fmt_frag_child(dest, fmt_frag(f, fmt_frag_type_ws_newline));
		} else if (s->s[i] == '#') {
			++i;
			cs = ce = i;
			for (; s->s[i] != '\n' && i < s->len; ++i) {
				++ce;
			}

			comment = (struct str){ .s = &s->s[cs], .len = ce - cs };

			child = fmt_frag_child(dest, fmt_frag_o(f, make_strn(f->wk, comment.s, comment.len)));
			if (trailing_comment) {
				child->type = fmt_frag_type_ws_comment_trailing;
				(*dest)->flags |= fmt_frag_flag_has_comment_trailing;
			} else {
				child->type |= fmt_frag_type_ws_comment;
			}

			obj stripped_comment = str_strip(f->wk, &comment, 0, 0);

			bool fmt_on;
			if (lexer_is_fmt_comment(get_str(f->wk, stripped_comment), &fmt_on)) {
				child->flags |= fmt_on ? fmt_frag_flag_fmt_on : fmt_frag_flag_fmt_off;
			}

			trailing_comment = false;
		}
	}
}

static const char *
fmt_node_to_token(enum node_type type)
{
	switch (type) {
	case node_type_continue: return "continue";
	case node_type_break: return "break";
	case node_type_return: return "return";
	case node_type_assign: return "=";
	case node_type_or: return "or";
	case node_type_and: return "and";
	case node_type_add: return "+";
	case node_type_sub: return "-";
	case node_type_mul: return "*";
	case node_type_div: return "/";
	case node_type_mod: return "%";
	case node_type_not: return "not";
	case node_type_eq: return "==";
	case node_type_neq: return "!=";
	case node_type_in: return "in";
	case node_type_not_in: return "not in";
	case node_type_lt: return "<";
	case node_type_gt: return ">";
	case node_type_leq: return "<=";
	case node_type_geq: return ">=";
	default: UNREACHABLE_RETURN;
	}
}

static int32_t
fmt_files_args_sort_cmp(const void *_a, const void *_b, void *_ctx)
{
	struct fmt_ctx *f = _ctx;
	const struct fmt_frag *a = *(const struct fmt_frag **)_a, *b = *(const struct fmt_frag **)_b;

	obj sa = fmt_frag_as_simple_str(f, a->child), sb = fmt_frag_as_simple_str(f, b->child);

	const char *s1 = get_cstr(f->wk, sa), *s2 = get_cstr(f->wk, sb);
	bool s1_hasdir = strchr(s1, '/') != NULL, s2_hasdir = strchr(s2, '/') != NULL;

	if ((s1_hasdir && s2_hasdir) || (!s1_hasdir && !s2_hasdir)) {
		return strcmp(s1, s2);
	} else if (s1_hasdir) {
		return -1;
	} else {
		return 1;
	}
}

static struct fmt_frag *fmt_block(struct fmt_ctx *f, struct node *n);
static struct fmt_frag *fmt_node(struct fmt_ctx *f, struct node *n);

enum fmt_list_flag {
	fmt_list_flag_sort_files = 1 << 0,
	fmt_list_flag_func_args = 1 << 1,
};

static void
fmt_list(struct fmt_ctx *f, struct node *n, struct fmt_frag *fr, enum fmt_list_flag flags)
{
	struct fmt_frag *child = 0, *next, *prev;
	obj str;

	enum node_type type = n->type;

	uint32_t list_tmp_base = f->list_tmp.len, len = 0;
	bool saw_trailing_comma = false;
	bool is_func_with_single_arg = false;

	while (true) {
		if (n->l) {
			prev = child;
			child = fmt_frag(f, fmt_frag_type_expr);
			arr_push(&f->list_tmp, &child);

			if (n->l->type == node_type_kw) {
				if (f->opts.kwargs_force_multiline) {
					fr->force_ml = true;
				}

				next = fmt_frag_child(&child->child, fmt_node(f, n->l->r));

				// Move the child's ws up one level and grab
				// post_ws if we have it.
				fmt_frag_move_ws(child, child->child);
				if (n->l->fmt.post.ws) {
					fmt_node_ws(f, n->l, n->l->fmt.post.ws, &child->post_ws);
				}

				if (type == node_type_def_args && n->l->r->l->data.type) {
					next = fmt_frag_child(&child->child,
						fmt_frag_s(f, typechecking_type_to_s(f->wk, n->l->r->l->data.type)));
					next->flags |= fmt_frag_flag_stick_line_left;
				}

				next = fmt_frag_child(&child->child, fmt_frag_s(f, ":"));
				if (!f->opts.wide_colon) {
					next->flags |= fmt_frag_flag_stick_left;
				} else {
					next->flags |= fmt_frag_flag_stick_line_left;
				}

				// node_type_list is a placeholder for keys with no value
				if (n->l->l->type != node_type_list) {
					next = fmt_frag_child(&child->child, fmt_frag(f, fmt_frag_type_expr));
					next->flags |= fmt_frag_flag_stick_line_left;
					fmt_frag_child(&next->child, fmt_node(f, n->l->l));
				}
			} else {
				fmt_frag_child(&child->child, fmt_node(f, n->l));

				// Move the child's ws up one level
				fmt_frag_move_ws(child, child->child);

				// If the previous list element is a plain
				// string that looks like a flag, mark the
				// current element as stick_line_left. This is
				// so that things like ['-o', value] remain
				// together when the list is multilined.
				if (prev && (str = fmt_frag_as_simple_str(f, prev->child))) {
					obj cur = fmt_frag_as_simple_str(f, child->child);
					const struct str *s = get_str(f->wk, str);
					bool prev_is_arg_like = str_startswith(s, &STR("-")) && !strchr(s->s, '='),
					     cur_is_arg_like = cur && str_startswith(get_str(f->wk, cur), &STR("-"));

					if (prev_is_arg_like && !cur_is_arg_like
						&& !(child->pre_ws && child->pre_ws->type == fmt_frag_type_ws_comment_trailing)) {
						child->flags |= fmt_frag_flag_stick_line_left;
					}
				}

				if (type == node_type_def_args && n->l->l->data.type) {
					next = fmt_frag_sibling(child->child,
						fmt_frag_s(f, typechecking_type_to_s(f->wk, n->l->l->data.type)));
					next->flags |= fmt_frag_flag_stick_line_left;
				}
			}

			++len;
		}

		if (!n->r) {
			break;
		}

		if (n->r->fmt.pre.ws) {
			// this means we got whitespace before the ,
			// Add it to the trailing whitespace for the current child
			fmt_node_ws(f, n, n->r->fmt.pre.ws, &child->post_ws);
		}

		assert(!child->str);
		child->str = make_str(f->wk, ",");
		n = n->r;

		if (!n->r && !n->l) {
			// trailing comma
			fr->force_ml = true;
			saw_trailing_comma = true;
		}
	}

	if (!saw_trailing_comma) {
		is_func_with_single_arg = (flags & fmt_list_flag_func_args) && len == 1;

		if (is_func_with_single_arg) {
			if (n->l->type == node_type_string
				&& str_startswith(get_str(f->wk, n->l->data.str), &STR("'''"))) {
				fr->flags |= fmt_frag_flag_force_single_line;
			}
		}

		if (f->opts.no_single_comma_function && is_func_with_single_arg) {
		} else {
			fr->flags |= fmt_frag_flag_add_trailing_comma;
		}
	}

	if (flags & fmt_list_flag_sort_files) {
		arr_sort_range(&f->list_tmp, list_tmp_base, f->list_tmp.len, f, fmt_files_args_sort_cmp);

		// Fix-up commas that might have gotten messed up by the above sorting
		uint32_t i;
		for (i = list_tmp_base; i < f->list_tmp.len; ++i) {
			child = *(struct fmt_frag **)arr_get(&f->list_tmp, i);
			if (i == f->list_tmp.len - 1) {
				if (saw_trailing_comma) {
					if (!child->str) {
						child->str = make_str(f->wk, ",");
					}
				} else {
					child->str = 0;
				}
			} else if (!child->str) {
				child->str = make_str(f->wk, ",");
			}
		}
	}

	uint32_t i;
	for (i = list_tmp_base; i < f->list_tmp.len; ++i) {
		child = *(struct fmt_frag **)arr_get(&f->list_tmp, i);
		fmt_frag_child(&fr->child, child);
	}

	f->list_tmp.len = list_tmp_base;
}

static struct fmt_frag *
fmt_node(struct fmt_ctx *f, struct node *n)
{
	assert(n->type != node_type_stmt);
	struct fmt_frag *fr, *res, *next;

	res = fr = fmt_frag(f, fmt_frag_type_expr);
	fr->node_type = n->type;

	if (n->fmt.pre.ws) {
		fmt_node_ws(f, n, n->fmt.pre.ws, &fr->pre_ws);
	}

	if (n->fmt.post.ws) {
		fmt_node_ws(f, n, n->fmt.post.ws, &res->post_ws);
	}

	/* L("formatting %p:%s", (void *)n, node_to_s(f->wk, n)); */

	switch (n->type) {
	case node_type_maybe_id:
	case node_type_stringify:
	case node_type_stmt: UNREACHABLE;

	case node_type_args:
	case node_type_def_args:
	case node_type_kw:
	case node_type_list:
	case node_type_foreach_args:
		// Skipped
		break;

	case node_type_continue:
	case node_type_break: {
		fr->str = make_str(f->wk, fmt_node_to_token(n->type));
		break;
	}
	case node_type_return: {
		fr->str = make_str(f->wk, fmt_node_to_token(n->type));
		if (n->l) {
			next = fmt_frag_sibling(fr, fmt_node(f, n->l));
			next->flags |= fmt_frag_flag_stick_line_left;
		}
		break;
	}

	case node_type_id:
	case node_type_id_lit:
	case node_type_number: {
		fr->str = n->data.str;
		break;
	}
	case node_type_string: {
		obj str;
		if (f->opts.simplify_string_literals && (str = fmt_obj_as_simple_str(f, n->data.str))
			&& !str_contains(get_str(f->wk, str), &STR("\n"))
			&& !str_contains(get_str(f->wk, str), &STR("'"))) {
			struct str newstr = *get_str(f->wk, n->data.str);
			if (str_startswith(&newstr, &STR("f"))) {
				++newstr.s;
				--newstr.len;
			}

			if (str_startswith(&newstr, &STR("'''"))) {
				newstr.s += 2;
				newstr.len -= 4;
			}

			n->data.str = make_strn(f->wk, newstr.s, newstr.len);
		}
		fr->str = n->data.str;
		break;
	}
	case node_type_bool: {
		fr->str = make_str(f->wk, n->data.num ? "true" : "false");
		break;
	}
	case node_type_null: {
		fr->str = make_str(f->wk, "null");
		break;
	}
	case node_type_negate: {
		fr->str = make_str(f->wk, "-");
		fr->flags |= fmt_frag_flag_stick_right;
		next = fmt_frag_sibling(fr, fmt_node(f, n->l));
		break;
	}
	case node_type_not: {
		fr->str = make_str(f->wk, "not");
		fr->flags |= fmt_frag_flag_stick_line_right;
		next = fmt_frag_sibling(fr, fmt_node(f, n->l));
		break;
	}
	case node_type_assign:
	case node_type_or:
	case node_type_and:
	case node_type_add:
	case node_type_sub:
	case node_type_mul:
	case node_type_div:
	case node_type_mod:
	case node_type_eq:
	case node_type_neq:
	case node_type_in:
	case node_type_not_in:
	case node_type_lt:
	case node_type_gt:
	case node_type_leq:
	case node_type_geq: {
		const char *token;
		if (n->type == node_type_assign && (n->data.type & op_store_flag_add_store)) {
			token = "+=";
		} else {
			token = fmt_node_to_token(n->type);
		}
		bool is_member_assign = n->type == node_type_assign && (n->data.type & op_store_flag_member);

		struct node *rhs;

		if (is_member_assign) {
			res = fr = fmt_node(f, n->r->l);
			if (n->l->type == node_type_id_lit) {
				next = fmt_frag_sibling(fr, fmt_frag_s(f, "."));
				next->flags |= fmt_frag_flag_stick_left;
				next = fmt_frag_sibling(fr, fmt_node(f, n->l));
				next->flags |= fmt_frag_flag_stick_left;
			} else {
				next = fmt_frag_sibling(fr, fmt_frag(f, fmt_frag_type_expr));
				next->enclosing = "[]";
				next->flags |= fmt_frag_flag_stick_left;
				fmt_frag_child(&next->child, fmt_node(f, n->l));
			}
			rhs = n->r->r;
		} else {
			res = fr = fmt_node(f, n->l);
			rhs = n->r;
		}

		next = fmt_frag_sibling(fr, fmt_frag_s(f, token));
		switch (n->type) {
		case node_type_add:
		case node_type_or:
		case node_type_and: {
			next->flags |= fmt_frag_flag_stick_line_left_unless_enclosed;
			break;
		}
		default: {
			next->flags |= fmt_frag_flag_stick_line_left;
			break;
		}
		}

		next = fmt_frag_sibling(fr, fmt_node(f, rhs));
		next->flags |= fmt_frag_flag_stick_line_left;
		break;
	}
	case node_type_index: {
		res = fr = fmt_node(f, n->l);
		next = fmt_frag_sibling(fr, fmt_frag(f, fmt_frag_type_expr));
		next->enclosing = "[]";
		next->flags |= fmt_frag_flag_stick_left;
		fmt_frag_child(&next->child, fmt_node(f, n->r));
		break;
	}
	case node_type_group: {
		fr->enclosing = "()";
		fmt_frag_child(&fr->child, fmt_node(f, n->l));
		if (f->opts.sticky_parens) {
			fr->child->flags |= fmt_frag_flag_stick_left;
			fmt_frag_last_child(fr)->flags |= fmt_frag_flag_stick_right;
		}
		break;
	}
	case node_type_dict: {
		fr->enclosing = "{}";
		fmt_list(f, n, fr, 0);
		break;
	}
	case node_type_array: {
		fr->enclosing = "[]";
		if (f->opts.space_array) {
			fr->flags |= fmt_frag_flag_enclosing_space;
		}

		fmt_list(f, n, fr, 0);
		break;
	}
	case node_type_member: {
		res = fr = fmt_node(f, n->l);
		next = fmt_frag_sibling(fr, fmt_frag_s(f, "."));
		next->flags |= fmt_frag_flag_stick_left;
		next = fmt_frag_sibling(fr, fmt_node(f, n->r));
		next->flags |= fmt_frag_flag_stick_left;

		/* fmt_list(f, n->l->l, next, fmt_list_flag_func_args); */
		/* next->enclosing = "()"; */
		/* next->flags |= fmt_frag_flag_stick_left; */
		break;
	}
	case node_type_call: {
		res = fr = fmt_node(f, n->r);

		enum fmt_list_flag flags = fmt_list_flag_func_args;

		{
			enum fmt_special_function {
				fmt_special_function_unknown,
				fmt_special_function_files,
			};

			enum fmt_special_function function = fmt_special_function_unknown;

			if (n->r->type == node_type_id_lit) {
				if (str_eql(get_str(f->wk, n->r->data.str), &STR("files"))) {
					function = fmt_special_function_files;
				}
			}

			switch (function) {
			case fmt_special_function_unknown: break;
			case fmt_special_function_files: {
				struct node *args = n->l, *arr;

				if (!f->opts.sort_files) {
					goto fmt_special_function_done;
				}

				// If files() gets a single argument of type
				// array, un-nest it.
				if (!args->r && args->l && args->l->type == node_type_array) {
					arr = args->l;
					args->l = arr->l;
					args->r = arr->r;
				}

				bool all_elements_are_simple_strings = true;

				while (true) {
					if (args->l) {
						if (args->l->type != node_type_string
							|| str_startswith(
								get_str(f->wk, args->l->data.str), &STR("f"))) {
							all_elements_are_simple_strings = false;
							break;
						}
					}

					if (!args->r) {
						break;
					}

					args = args->r;
				}

				if (all_elements_are_simple_strings) {
					flags |= fmt_list_flag_sort_files;
				}
				break;
			}
			}
		}

fmt_special_function_done:

		next = fmt_frag_sibling(fr, fmt_frag(f, fmt_frag_type_expr));
		fmt_list(f, n->l, next, flags);
		next->enclosing = "()";
		next->flags |= fmt_frag_flag_stick_left;
		break;
	}
	case node_type_ternary: {
		res = fr = fmt_node(f, n->l);
		next = fmt_frag_sibling(fr, fmt_frag_s(f, "?"));
		next->flags |= fmt_frag_flag_stick_line_left;
		next = fmt_frag_sibling(fr, fmt_node(f, n->r->l));
		next->flags |= fmt_frag_flag_stick_line_left;
		next = fmt_frag_sibling(fr, fmt_frag_s(f, ":"));
		next->flags |= fmt_frag_flag_stick_line_left;
		next = fmt_frag_sibling(fr, fmt_node(f, n->r->r));
		next->flags |= fmt_frag_flag_stick_line_left;
		break;
	}
	case node_type_foreach: {
		res->type = fmt_frag_type_lines;
		fr = fmt_frag_child(&res->child, fmt_frag(f, fmt_frag_type_line));
		fr = fmt_frag_child(&fr->child, fmt_frag_s(f, "foreach"));

		next = fmt_frag_sibling(fr, fmt_node(f, n->l->l->l));
		next->flags |= fmt_frag_flag_stick_line_left;
		if (n->l->l->r) {
			str_app(f->wk, &next->str, ",");
			next = fmt_frag_sibling(fr, fmt_node(f, n->l->l->r));
			next->flags |= fmt_frag_flag_stick_line_left;
		}

		next = fmt_frag_sibling(fr, fmt_frag_s(f, ":"));
		next->flags |= fmt_frag_flag_stick_line_left;

		next = fmt_frag_sibling(fr, fmt_node(f, n->l->r));
		next->flags |= fmt_frag_flag_stick_line_left;

		if (n->r) {
			fmt_frag_child(&res->child, fmt_block(f, n->r));
		}

		fr = fmt_frag_child(&res->child, fmt_frag(f, fmt_frag_type_line));
		fmt_frag_child(&fr->child, fmt_frag_s(f, "endforeach"));
		break;
	}
	case node_type_if: {
		res->type = fmt_frag_type_lines;

		bool first = true;
		while (n) {
			fr = fmt_frag_child(&res->child, fmt_frag(f, fmt_frag_type_line));
			fr = fmt_frag_child(&fr->child, fmt_frag_s(f, first ? "if" : n->l->l ? "elif" : "else"));

			if (n->l->l) {
				next = fmt_frag_sibling(fr, fmt_node(f, n->l->l));
				next->flags |= fmt_frag_flag_stick_line_left;
				if (f->opts.continuation_indent) {
					fmt_frag_broadcast_flag(next, fmt_frag_flag_enclosed_extra_indent);
				}
			}

			if (n->l->r) {
				fmt_frag_child(&res->child, fmt_block(f, n->l->r));
			}

			first = false;
			n = n->r;
		}

		fr = fmt_frag_child(&res->child, fmt_frag(f, fmt_frag_type_line));
		fmt_frag_child(&fr->child, fmt_frag_s(f, "endif"));
		break;
	}
	case node_type_func_def: {
		res->type = fmt_frag_type_lines;
		fr = fmt_frag_child(&res->child, fmt_frag(f, fmt_frag_type_line));
		fr = fmt_frag_child(&fr->child, fmt_frag_s(f, "func"));
		if (n->l->l->l) {
			next = fmt_frag_sibling(fr, fmt_node(f, n->l->l->l));
			next->flags |= fmt_frag_flag_stick_line_left;
		}
		next = fmt_frag_sibling(fr, fmt_frag(f, fmt_frag_type_expr));
		fmt_list(f, n->l->r, next, 0);
		next->enclosing = "()";
		next->flags |= fmt_frag_flag_stick_left;

		if (n->data.type) {
			next = fmt_frag_sibling(fr, fmt_frag_s(f, "->"));
			next->flags |= fmt_frag_flag_stick_line_left;
			next = fmt_frag_sibling(fr, fmt_frag_s(f, typechecking_type_to_s(f->wk, n->data.type)));
			next->flags |= fmt_frag_flag_stick_line_left;
		}

		if (n->r) {
			fmt_frag_child(&res->child, fmt_block(f, n->r));
		}

		fr = fmt_frag_child(&res->child, fmt_frag(f, fmt_frag_type_line));
		fmt_frag_child(&fr->child, fmt_frag_s(f, "endfunc"));
		break;
	}
	}

	return res;
}

static struct fmt_frag *
fmt_block(struct fmt_ctx *f, struct node *n)
{
	struct fmt_frag *block, *line, *child;

	block = fmt_frag(f, fmt_frag_type_block);
	block->force_ml = true;

	while (true) {
		assert(n->type == node_type_stmt);

		line = fmt_frag(f, fmt_frag_type_line);

		if (n->fmt.pre.ws) {
			fmt_node_ws(f, n, n->fmt.pre.ws, &line->pre_ws);
		}

		if (n->fmt.post.ws) {
			fmt_node_ws(f, n, n->fmt.post.ws, &line->post_ws);
		}

		if (n->l) {
			child = fmt_node(f, n->l);

			if (child->type == fmt_frag_type_lines) {
				child->child->pre_ws = line->pre_ws;
				fmt_frag_last_child(child)->post_ws = line->post_ws;

				fmt_frag_child(&block->child, child->child);
			} else {
				fmt_frag_child(&line->child, child);
				fmt_frag_child(&block->child, line);
			}
		} else {
			// This is an empty line, only add empty lines if they have pre_ws
			if (line->pre_ws) {
				// Convert all trailing comments on empty lines into regular comments
				if (line->pre_ws->flags & fmt_frag_flag_has_comment_trailing) {
					for (child = line->pre_ws; child; child = child->next) {
						if (child->type == fmt_frag_type_ws_comment_trailing) {
							child->type = fmt_frag_type_ws_comment;
						}
					}

					line->pre_ws->flags &= ~fmt_frag_flag_has_comment_trailing;
				}

				fmt_frag_child(&block->child, line);
			}
		}

		if (!n->r) {
			break;
		}

		n = n->r;
	}

	if (!block->child) {
		// If we don't have any children, then disregard this block
		return 0;
	}

	return block;
}

/*******************************************************************************
 * config parsing
 ******************************************************************************/

static void
fmt_cfg_parse_indent_by(struct fmt_ctx *f, void *val)
{
	const char *indent_by = *(const char **)val;

	if (!*indent_by) {
		f->opts.indent_size = 0;
		return;
	}

	if (*indent_by == ' ') {
		f->opts.indent_style = fmt_indent_style_space;
		for (; *indent_by; ++indent_by) {
			++f->opts.indent_size;
		}
	} else if (*indent_by == '\t') {
		f->opts.indent_style = fmt_indent_style_tab;
	}
}

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
		type_enum,
	};

	struct fmt_cfg_enum {
		const char *name;
		uint32_t val;
	};

	struct fmt_cfg_enum indent_style_tbl[]
		= { { "tab", fmt_indent_style_tab }, { "space", fmt_indent_style_space }, 0 };

	struct fmt_cfg_enum end_of_line_tbl[]
		= { { "lf", fmt_end_of_line_lf }, { "cr", fmt_end_of_line_cr }, { "crlf", fmt_end_of_line_crlf }, 0 };

	const struct {
		const char *name;
		enum val_type type;
		uint32_t off;
		bool deprecated;
		void((*deprecated_action)(struct fmt_ctx *f, void *val));
		struct fmt_cfg_enum *enum_tbl;
	} keys[] = {
		{ "max_line_len", type_uint, offsetof(struct fmt_opts, max_line_len) },
		{ "space_array", type_bool, offsetof(struct fmt_opts, space_array) },
		{ "kwargs_force_multiline", type_bool, offsetof(struct fmt_opts, kwargs_force_multiline) },
		{ "wide_colon", type_bool, offsetof(struct fmt_opts, wide_colon) },
		{ "no_single_comma_function", type_bool, offsetof(struct fmt_opts, no_single_comma_function) },
		{ "insert_final_newline", type_bool, offsetof(struct fmt_opts, insert_final_newline) },
		{ "sort_files", type_bool, offsetof(struct fmt_opts, sort_files) },
		{ "group_arg_value", type_bool, offsetof(struct fmt_opts, group_arg_value) },
		{ "simplify_string_literals", type_bool, offsetof(struct fmt_opts, simplify_string_literals) },
		{ "use_editor_config", type_bool, offsetof(struct fmt_opts, use_editor_config) },
		{ "indent_before_comments", type_str, offsetof(struct fmt_opts, indent_before_comments) },
		{ "indent_size", type_uint, offsetof(struct fmt_opts, indent_size) },
		{ "tab_width", type_uint, offsetof(struct fmt_opts, tab_width) },
		{ "indent_style", type_enum, offsetof(struct fmt_opts, indent_style), .enum_tbl = indent_style_tbl },
		{ "end_of_line", type_enum, offsetof(struct fmt_opts, end_of_line), .enum_tbl = end_of_line_tbl },
		{ "sticky_parens", type_bool, offsetof(struct fmt_opts, sticky_parens) },
		{ "continuation_indent", type_bool, offsetof(struct fmt_opts, continuation_indent) },

		// deprecated options
		{ "indent_by", type_str, .deprecated = true, .deprecated_action = fmt_cfg_parse_indent_by },
		{ "kwa_ml", type_bool, offsetof(struct fmt_opts, kwargs_force_multiline), .deprecated = true },

		0,
	};

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
		if (strcmp(k, keys[i].name) != 0) {
			continue;
		}

		void *val_dest = (((uint8_t *)(&ctx->opts)) + keys[i].off);

		if (keys[i].deprecated) {
			error_messagef(src, location, log_warn, "option %s is deprecated", keys[i].name);
		}

		switch (keys[i].type) {
		case type_uint: {
			char *endptr = NULL;
			long long lval = strtoll(v, &endptr, 10);
			if (*endptr) {
				error_messagef(src, location, log_error, "unable to parse integer");
				return false;
			} else if (lval < 0 || lval > (long long)UINT32_MAX) {
				error_messagef(src, location, log_error, "integer outside of range 0-%u", UINT32_MAX);
				return false;
			}

			uint32_t val = lval;

			if (keys[i].deprecated_action) {
				keys[i].deprecated_action(ctx, &val);
			} else {
				memcpy(val_dest, &val, sizeof(uint32_t));
			}
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

			if (keys[i].deprecated_action) {
				keys[i].deprecated_action(ctx, &start);
			} else {
				memcpy(val_dest, &start, sizeof(char *));
			}
			break;
		}
		case type_bool: {
			bool val;
			if (strcmp(v, "true") == 0) {
				val = true;
			} else if (strcmp(v, "false") == 0) {
				val = false;
			} else {
				error_messagef(src, location, log_error, "invalid value for bool, expected true/false");
				return false;
			}

			if (keys[i].deprecated_action) {
				keys[i].deprecated_action(ctx, &val);
			} else {
				memcpy(val_dest, &val, sizeof(bool));
			}
			break;
		}
		case type_enum: {
			assert(keys[i].enum_tbl);

			uint32_t j, val = 0;
			for (j = 0; keys[i].enum_tbl[j].name; ++j) {
				if (strcmp(v, keys[i].enum_tbl[j].name) == 0) {
					val = keys[i].enum_tbl[j].val;
					break;
				}
			}

			if (!keys[i].enum_tbl[j].name) {
				error_messagef(src, location, log_error, "invalid value for %s: %s", keys[i].name, v);
				return false;
			}

			if (keys[i].deprecated_action) {
				keys[i].deprecated_action(ctx, &val);
			} else {
				memcpy(val_dest, &val, sizeof(uint32_t));
			}
		}
		}

		break;
	}

	if (!keys[i].name) {
		error_messagef(src, location, log_error, "unknown config key: %s", k);
		return false;
	}

	return true;
}

static enum fmt_end_of_line
fmt_guess_line_endings(struct source *src)
{
	uint32_t i;
	for (i = 0; i < src->len; ++i) {
		if (strncmp(&src->src[i], "\r\n", 2) == 0) {
			return fmt_end_of_line_crlf;
		} else if (src->src[i] == '\n') {
			return fmt_end_of_line_lf;
		}
	}

	return fmt_end_of_line_lf;
}

/*******************************************************************************
 * entrypoint
 ******************************************************************************/

void
fmt_assemble_out_blocks(struct fmt_ctx *f)
{
	uint32_t i;
	struct fmt_out_block *blocks = (struct fmt_out_block *)f->out_blocks.e;
	const struct str *s;
	const char *line, *line_end;
	struct str lstr;
	obj l;
	bool end_of_block;

	L("fmt output: ");

	const char *end_of_line = 0;
	switch ((enum fmt_end_of_line)f->opts.end_of_line) {
	case fmt_end_of_line_lf: end_of_line = "\n"; break;
	case fmt_end_of_line_crlf: end_of_line = "\r\n"; break;
	case fmt_end_of_line_cr: end_of_line = "\r"; break;
	default: UNREACHABLE;
	}

	for (i = 0; i < f->out_blocks.len; ++i) {
		s = get_str(f->wk, blocks[i].str);
		if (blocks[i].raw) {
			tstr_pushn(f->wk, f->out_buf, s->s, s->len);
			continue;
		}

		for (line = s->s; *line;) {
			lstr.s = line;
			line_end = strchr(line, '\n');
			end_of_block = false;

			if (line_end == line) {
				tstr_pushs(f->wk, f->out_buf, end_of_line);
				++line;
				continue;
			} else if (line_end) {
				lstr.len = line_end - line;
			} else {
				lstr.len = strlen(line);
			}

			end_of_block = !*(line + lstr.len);

			l = str_strip(f->wk, &lstr, 0, str_strip_flag_right_only);
			s = get_str(f->wk, l);

			tstr_pushn(f->wk, f->out_buf, s->s, s->len);

			bool last_line = end_of_block && i == f->out_blocks.len - 1;

			if (last_line) {
				if (f->opts.insert_final_newline) {
					tstr_pushs(f->wk, f->out_buf, end_of_line);
				}
			} else if (!end_of_block) {
				tstr_pushs(f->wk, f->out_buf, end_of_line);
			}

			if (!line_end) {
				break;
			}

			line = line_end + 1;
		}
	}
}

bool
fmt(struct source *src, FILE *out, const char *cfg_path, bool check_only, bool editorconfig)
{
	bool ret = false;
	struct tstr out_buf;
	struct workspace wk = { 0 };
	workspace_init_bare(&wk);
	struct fmt_ctx f = {
		.wk = &wk,
		.out_buf = &out_buf,
		.fmt_on = true,
		.opts = {
			.max_line_len = 80,
			.indent_style = fmt_indent_style_space,
			.indent_size = 4,
			.tab_width = 8,
			.space_array = false,
			.kwargs_force_multiline = false,
			.wide_colon = false,
			.no_single_comma_function = false,
			.insert_final_newline = true,
			.end_of_line = fmt_guess_line_endings(src),
			.sort_files = true,
			.group_arg_value = true,
			.simplify_string_literals = false,
			.indent_before_comments = " ",
			.use_editor_config = true,
			.sticky_parens = false,
			.continuation_indent = false,
		},
	};

	bucket_arr_init(&f.frags, 1024, sizeof(struct fmt_frag));
	arr_init(&f.out_blocks, 64, sizeof(struct fmt_out_block));
	arr_init(&f.list_tmp, 64, sizeof(struct fmt_frag *));

	if (editorconfig) {
		try_parse_editorconfig(src, &f.opts);
	}

	char *cfg_buf = NULL;
	struct source cfg_src = { 0 };
	if (cfg_path) {
		if (!ini_parse(cfg_path, &cfg_src, &cfg_buf, fmt_cfg_parse_cb, &f)) {
			goto ret;
		}
	}

	enum vm_compile_mode compile_mode = vm_compile_mode_fmt;
	if (str_endswith(&STRL(src->label), &STR(".meson"))) {
		compile_mode |= vm_compile_mode_language_extended;
	}

	struct node *n;
	if (!(n = parse_fmt(&wk, src, compile_mode, &f.raw_blocks))) {
		goto ret;
	}

	tstr_init(&out_buf, 0, 0, 0);
	fmt_write_block(&f, fmt_block(&f, n));
	fmt_push_out_block(&f);

	if (!check_only) {
		out_buf.flags = tstr_flag_write;
		out_buf.buf = (void *)out;
	}
	fmt_assemble_out_blocks(&f);

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
	fs_source_destroy(&cfg_src);
	bucket_arr_destroy(&f.frags);
	arr_destroy(&f.out_blocks);
	arr_destroy(&f.list_tmp);
	return ret;
}
