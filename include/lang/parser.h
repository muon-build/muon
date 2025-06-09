/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_PARSER_H
#define MUON_LANG_PARSER_H

#include <stdbool.h>

#include "lang/compiler.h"
#include "lang/lexer.h"

enum node_type {
	node_type_stmt,
	node_type_group,
	node_type_bool,
	node_type_null,
	node_type_id,
	node_type_maybe_id,
	node_type_id_lit,
	node_type_number,
	node_type_string,
	node_type_continue,
	node_type_break,
	node_type_args,
	node_type_def_args,
	node_type_dict,
	node_type_array,
	node_type_list,
	node_type_kw,
	node_type_or,
	node_type_and,
	node_type_eq,
	node_type_neq,
	node_type_lt,
	node_type_leq,
	node_type_gt,
	node_type_geq,
	node_type_in,
	node_type_not_in,
	node_type_add,
	node_type_sub,
	node_type_div,
	node_type_mul,
	node_type_mod,
	node_type_not,
	node_type_index,
	node_type_member,
	node_type_call,
	node_type_assign,
	node_type_foreach,
	node_type_foreach_args,
	node_type_if,
	node_type_negate,
	node_type_ternary,
	node_type_stringify,
	node_type_func_def,
	node_type_return,
};

struct node_fmt {
	obj ws;
};

enum node_flag {
	node_flag_breakpoint = 1 << 0,
};

struct node {
	union literal_data data;
	struct node *l, *r;
	struct source_location location;
	struct {
		struct node_fmt pre, post;
	} fmt;
	uint16_t type;
	uint16_t flags;
};

void print_ast(struct workspace *wk, struct node *root);
void print_fmt_ast(struct workspace *wk, struct node *root);
struct node *parse(struct workspace *wk, const struct source *src, enum vm_compile_mode mode);
struct node *parse_fmt(struct workspace *wk, const struct source *src, enum vm_compile_mode mode, obj *raw_blocks);
const char *node_type_to_s(enum node_type t);
const char *node_to_s(struct workspace *wk, const struct node *n);

struct node *cm_parse(struct workspace *wk, const struct source *src);
#endif
