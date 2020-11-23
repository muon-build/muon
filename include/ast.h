#ifndef BOSON_AST_H
#define BOSON_AST_H

#include <stddef.h>
#include <stdbool.h>

enum ast_expression_type {
	EXPRESSION_NONE,
	EXPRESSION_ASSIGNMENT,
	EXPRESSION_CONDITION,
	EXPRESSION_OR,
	EXPRESSION_AND,
	EXPRESSION_EQUALITY,
	EXPRESSION_RELATION,
	EXPRESSION_ADDITION,
	EXPRESSION_MULTIPLICATION,
	EXPRESSION_UNARY,
	EXPRESSION_SUBSCRIPT,
	EXPRESSION_FUNCTION,
	EXPRESSION_METHOD,
	EXPRESSION_IDENTIFIER,
	EXPRESSION_STRING,
	EXPRESSION_ARRAY,
	EXPRESSION_BOOL,
};

struct ast_expression;

struct ast_bool {
	bool value;
};

struct ast_identifier {
	char *data;
	size_t n;
};

struct ast_string {
	char *data;
	size_t n;
};

struct ast_identifier_list {
	struct ast_identifier **identifiers;
	size_t n;
};

struct ast_expression_list {
	struct ast_expression **expressions;
	size_t n;
};

struct ast_keyword_list {
	struct ast_identifier **keys;
	struct ast_expression **values;
	size_t n;
};

struct ast_arguments {
	struct ast_expression_list *args;
	struct ast_keyword_list *kwargs;
};

struct ast_subscript {
	struct ast_expression *left;
	struct ast_expression *right;
};

struct ast_function {
	struct ast_identifier *left;
	struct ast_arguments *right;
};

struct ast_method {
	struct ast_expression *left;
	struct ast_expression *right;
};

enum ast_unary_op {
	UNARY_NOT,
	UNARY_PLUS,
	UNARY_MINUS,
};

struct ast_unary {
	enum ast_unary_op op;
	struct ast_expression *right;
};

enum ast_multiplication_op {
	MULTIPLICATION_STAR = 1 << 0,
	MULTIPLICATION_SLASH = 1 << 1,
	MULTIPLICATION_MOD = 1 << 2,
};

struct ast_multiplication {
	struct ast_expression *left;
	enum ast_multiplication_op op;
	struct ast_expression *right;
};

enum ast_addition_op {
	ADDITION_PLUS = 1 << 0,
	ADDITION_MINUS = 1 << 1,
};

struct ast_addition {
	struct ast_expression *left;
	enum ast_addition_op op;
	struct ast_expression *right;
};

enum ast_relation_op {
	RELATION_GT = 1 << 0,
	RELATION_LT = 1 << 1,
	RELATION_GEQ = 1 << 2,
	RELATION_LEQ = 1 << 3,
	RELATION_IN = 1 << 4,
	RELATION_NIN = 1 << 5,
};

struct ast_relation {
	struct ast_expression *left;
	enum ast_relation_op op;
	struct ast_expression *right;
};

enum ast_equality_op {
	EQUALITY_EQ = 1 << 0,
	EQUALITY_NEQ = 1 << 1
};

struct ast_equality {
	struct ast_expression *equality;
	enum ast_equality_op op;
	struct ast_expression *relation;
};

struct ast_and {
	struct ast_expression *left;
	struct ast_expression *right;
};

struct ast_or {
	struct ast_expression *left;
	struct ast_expression *right;
};

struct ast_condition {
	struct ast_expression *cond;
	struct ast_expression *left;
	struct ast_expression *right;
};

enum ast_assignment_op {
	ASSIGNMENT_ASSIGN = 1 << 0,
	ASSIGNMENT_STAREQ = 1 << 1,
	ASSIGNMENT_SLASHEQ = 1 << 2,
	ASSIGNMENT_MODEQ = 1 << 3,
	ASSIGNMENT_PLUSEQ = 1 << 4,
	ASSIGNMENT_MINEQ = 1 << 5,
};

struct ast_assignment {
	struct ast_expression *left;
	enum ast_assignment_op op;
	struct ast_expression *right;
};

struct ast_expression {
	enum ast_expression_type type;
	union {
		struct ast_assignment *assignment;
		struct ast_condition *condition;
		struct ast_or *or;
		struct ast_and *and;
		struct ast_equality *equality;
		struct ast_relation *relation;
		struct ast_addition *addition;
		struct ast_multiplication *multiplication;
		struct ast_unary *unary;
		struct ast_subscript *subscript;
		struct ast_function *function;
		struct ast_method *method;
		struct ast_identifier *identifier;
		struct ast_string *string;
		struct ast_expression_list *array;
		struct ast_bool *boolean;
	} data;
};

struct ast_selection {
	int dummy;
};

struct ast_iteration {
	int dummy;
};

enum ast_statement_type {
	STATEMENT_EXPRESSION = 1 << 0,
	STATEMENT_SELECTION = 1 << 1,
	STATEMENT_ITERATION = 1 << 2,
};

struct ast_statement {
	enum ast_statement_type type;
	union {
		struct ast_expression *expression;
		struct ast_selection *selection;
		struct ast_iteration *iteration;
	} data;
};

const char *ast_expression_to_str(struct ast_expression *);

#endif // BOSON_AST_H
