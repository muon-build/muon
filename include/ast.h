#ifndef BOSON_AST_H
#define BOSON_AST_H

#include <stdbool.h>

enum node_expression_type {
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
};

struct node_expression;

struct node_identifier {
	char *data;
	size_t n;
};

struct node_string {
	char *data;
	size_t n;
};

struct node_identifier_list {
	struct node_identifier **identifiers;
	size_t n;
};

struct node_expression_list {
	struct node_expression **expressions;
	size_t n;
};

struct node_keyword_list {
	struct node_expression **keys;
	struct node_expression **values;
	size_t n;
};

struct node_arguments {
	struct node_expression_list *args;
	struct node_keyword_list *kwargs;
};

struct node_subscript {
	struct node_expression *left;
	struct node_expression *right;
};

struct node_function {
	struct node_identifier *left;
	struct node_arguments *right;
};

struct node_method {
	struct node_expression *left;
	struct node_function *right;
};

enum node_unary_op {
	UNARY_NOT,
	UNARY_PLUS,
	UNARY_MINUS,
};

struct node_unary {
	enum node_unary_op op;
	struct node_expression *right;
};

enum node_multiplication_op {
	MULTIPLICATION_STAR = 1 << 0,
	MULTIPLICATION_SLASH = 1 << 1,
	MULTIPLICATION_MOD = 1 << 2,
};

struct node_multiplication {
	struct node_expression *left;
	enum node_multiplication_op op;
	struct node_expression *right;
};

enum node_addition_op {
	ADDITION_PLUS = 1 << 0,
	ADDITION_MINUS = 1 << 1,
};

struct node_addition {
	struct node_expression *left;
	enum node_addition_op op;
	struct node_expression *right;
};

enum node_relation_op {
	RELATION_GT = 1 << 0,
	RELATION_LT = 1 << 1,
	RELATION_GEQ = 1 << 2,
	RELATION_LEQ = 1 << 3,
	RELATION_IN = 1 << 4,
	RELATION_NIN = 1 << 5,
};

struct node_relation {
	struct node_expression *left;
	enum node_relation_op op;
	struct node_expression *right;
};

enum node_equality_op {
	EQUALITY_EQ = 1 << 0,
	EQUALITY_NEQ = 1 << 1
};

struct node_equality {
	struct node_expression *equality;
	enum node_equality_op op;
	struct node_expression *relation;
};

struct node_and {
	struct node_expression *left;
	struct node_expression *right;
};

struct node_or {
	struct node_expression *left;
	struct node_expression *right;
};

struct node_condition {
	struct node_expression *cond;
	struct node_expression *left;
	struct node_expression *right;
};

enum node_assignment_op {
	ASSIGNMENT_ASSIGN = 1 << 0,
	ASSIGNMENT_STAREQ = 1 << 1,
	ASSIGNMENT_SLASHEQ = 1 << 2,
	ASSIGNMENT_MODEQ = 1 << 3,
	ASSIGNMENT_PLUSEQ = 1 << 4,
	ASSIGNMENT_MINEQ = 1 << 5,
};

struct node_assignment {
	struct node_expression *left;
	enum node_assignment_op op;
	struct node_expression *right;
};

struct node_expression {
	enum node_expression_type type;
	union {
		struct node_assignment *assigment;
		struct node_condition *condition;
		struct node_or *or;
		struct node_and *and;
		struct node_equality *equality;
		struct node_relation *relation;
		struct node_addition *addition;
		struct node_multiplication *multiplication;
		struct node_unary *unary;
		struct node_subscript *subscript;
		struct node_function *function;
		struct node_method *method;
		struct node_identifier *identifier;
		struct node_string *string;
		struct node_expression_list *array;
	} data;
};

struct node_selection {
	int dummy;
};

struct node_iteration {
	int dummy;
};

enum node_statement_type {
	STATEMENT_EXPRESSION = 1 << 0,
	STATEMENT_SELECTION = 1 << 1,
	STATEMENT_ITERATION = 1 << 2,
};

struct node_statement {
	enum node_statement_type type;
	union {
		struct node_expression *expression;
		struct node_selection *selection;
		struct node_iteration *iteration;
	} data;
};

#endif // BOSON_AST_H
