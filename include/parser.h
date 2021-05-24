#ifndef BOSON_PARSER_H
#define BOSON_PARSER_H

#include "darr.h"
#include "lexer.h"

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

enum if_type {
	if_normal,
	if_else,
};

enum node_type {
	node_null, // only used for parsing
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
	node_foreach,
	node_foreach_args,
	node_if,
	node_u_minus,
	node_ternary,
	node_block,
};

enum node_child_flag {
	node_child_l = 1 << 0,
	node_child_r = 1 << 1,
	node_child_c = 1 << 2,
	node_child_d = 1 << 3,
};

struct node {
	enum node_type type;
	struct token *tok;
	uint32_t data;
	uint32_t l, r, c, d;
	uint8_t chflg;
};

struct ast {
	struct tokens *toks;
	struct darr nodes;
	uint32_t root;
};

bool parser_parse(struct ast *ast, struct tokens *toks);
void print_ast(struct ast *ast);
struct node *get_node(struct ast *ast, uint32_t i);
const char *node_to_s(struct node *n);
const char *node_type_to_s(enum node_type t);
const char *source_location(struct ast *ast, uint32_t id);
void ast_destroy(struct ast *ast);
#endif // BOSON_PARSER_H
