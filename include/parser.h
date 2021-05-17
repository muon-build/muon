#ifndef BOSON_PARSER_H
#define BOSON_PARSER_H

#include "darr.h"

#include <stdint.h>
#include <stdbool.h>

enum comparison_type {
	comp_equal,
	comp_nequal,
	comp_lt,
	comp_le,
	comp_gt,
	comp_ge,
	comp_in,
	comp_not_in // keep at the end,
};

enum arithmetic_type {
	arith_add = 0,
	arith_sub = 1,
	arith_mod = 2,
	arith_mul = 3,
	arith_div = 4,
};

enum arg_type {
	arg_normal,
	arg_kwarg,
};

enum node_type {
	node_any, // used for argument parsing in interperter
	node_bool,
	node_id,
	node_number,
	node_string,
	node_format_string,
	node_continue,
	node_break,
	node_argument,
	node_array,
	node_dict,
	node_empty,
	node_or,
	node_and,
	node_comparison,
	node_arithmetic,
	node_not,
	node_index,
	node_method,
	node_function,
	node_assignment,
	node_plus_assignment,
	node_foreach_clause,
	node_if,
	node_if_clause,
	node_u_minus,
	node_ternary,
};

enum node_child_flag {
	node_child_l = 1 << 0,
	node_child_r = 1 << 1,
	node_child_c = 1 << 2,
};

struct node {
	enum node_type type;
	struct token *tok;
	uint32_t data;
	uint32_t l, r, c;
	uint8_t chflg;
};

struct ast {
	struct darr nodes, ast;
};

bool parse(struct ast *ast, const char *source_dir);
void print_tree(struct ast *ast, uint32_t id, uint32_t d);
const char *node_to_s(struct node *n);
const char *node_type_to_s(enum node_type t);
const char *source_location(struct ast *ast, uint32_t id);

#endif // BOSON_PARSER_H
