#include "parser.h"
#include "ast.h"
#include "lexer.h"
#include "token.h"
#include "log.h"

#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define PATH_MAX 4096

struct parser
{
	struct lexer lexer;
	struct token *cur;
	struct token *last;
};

static bool
accept(struct parser *parser, enum token_type type) {
	if (parser->cur->type == type) {
		free(parser->last);
		parser->last = parser->cur;
		parser->cur = lexer_tokenize(&parser->lexer);
		return true;
	}
	return false;
}

static void
expect(struct parser *parser, enum token_type type)
{
	if (!accept(parser, type)) {
		fatal("expected %s, got %s", token_type_to_string(type),
				token_to_string(parser->cur));
	}
}

struct node_expression *parse_expression(struct parser *);

/* TODO move in ninja emitter
static const char *
is_function(struct token *token)
{
	if (token->type != TOKEN_IDENTIFIER) {
		return NULL;
	}

	* Keep in order *
	static const char *funcs[] = {
		"add_global_arguments", "add_global_link_arguments",
		"add_languages", "add_project_arguments", "add_project_link_arguments",
		"add_test_setup", "alias_target", "assert", "benchmark",
		"both_libraries", "build_target", "configuration_data",
		"configure_file", "custom_target", "declare_dependency", "dependency",
		"disabler", "environment", "error", "executable", "files",
		"find_library", "find_program", "generator", "get_option",
		"get_variable", "gettext", "import", "include_directories",
		"install_data", "install_headers", "install_man", "install_subdir",
		"is_disabler", "is_variable", "jar", "join_paths", "library",
		"message", "option", "project", "run_command", "run_target",
		"set_variable", "shared_library", "shared_module", "static_library",
		"subdir", "subdir_done", "subproject", "summary", "test", "vcs_tag",
		"warning",
	};

	int low = 0, high = (sizeof(funcs) / sizeof(funcs[0])) - 1, mid, cmp;
	while (low <= high) {
		mid = (low + high) / 2;
		cmp = strcmp(token->data, funcs[mid]);
		if (cmp == 0) {
			return funcs[mid];
		}
		if (cmp < 0) {
			high = mid - 1;
		}
		else {
			low = mid + 1;
		}
	}

	return NULL;
}
*/

void
expression_list_appened(struct node_expression_list *list,
		struct node_expression *expression)
{
	if (list == NULL) {
		fatal("cannot appened expression to empty list");
	}

	list->n = list->n + 1;

	list->expressions = realloc(list->expressions,
			list->n * sizeof(struct node_expression));
	list->expressions[list->n] = expression;
}

void
keyword_list_appened(struct node_keyword_list *list,
		struct node_expression *key, struct node_expression *value)
{
	if (list == NULL) {
		fatal("cannot appened keyword to empty list");
	}

	list->n = list->n + 1;
	list->keys = realloc(list->keys,
			list->n * sizeof(struct node_expression));
	list->values = realloc(list->values,
			list->n * sizeof(struct node_expression));
	list->keys[list->n] = key;
	list->values[list->n] = value;
}

struct node_identifier *
parse_identifier(struct parser *parser)
{
	struct node_identifier *identifier = calloc(1,
			sizeof(struct node_identifier));
	if (!identifier) {
		fatal("failed to allocate identifier node");
	}

	identifier->data = calloc(parser->last->n + 1, sizeof(char));
	strncpy(identifier->data, parser->last->data, parser->last->n);
	identifier->n = parser->last->n;

	return identifier;
}

struct node_string *
parse_string(struct parser *parser)
{
	struct node_string *string = calloc(1, sizeof(struct node_string));
	if (!string) {
		fatal("failed to allocate string node");
	}

	string->data = calloc(parser->last->n + 1, sizeof(char));
	strncpy(string->data, parser->last->data, parser->last->n);
	string->n = parser->last->n;

	return string;
}

/*
 * An array is a list containing an arbitrary number of any types
 * It is delimited by brackets, and separated by commas.
 *
 * arr = [1, 2, 3, 'soleil']
 */
struct node_expression_list *
parse_array(struct parser *parser)
{
	struct node_expression_list *list = calloc(1,
			sizeof(struct node_expression_list));
	if (!list) {
		fatal("failed to allocate array");
	}

	for (;;) {
		while (accept(parser, TOKEN_EOL));

		if (accept(parser, TOKEN_RBRACK)) {
			break;
		}

		expression_list_appened(list, parse_expression(parser));

		if (accept(parser, TOKEN_RBRACK)) {
			break;
		}

		expect(parser, TOKEN_COMMA);
	}

	return list;
}

struct node_bool *
parse_bool(struct parser *parser)
{
	struct node_bool *boolean = calloc(1, sizeof(struct node_bool));
	assert(boolean);

	if (parser->last->type == TOKEN_TRUE) {
		boolean->value = true;
	} else if (parser->last->type == TOKEN_FALSE) {
		boolean->value = false;
	}

	return boolean;
}

struct node_expression *
parse_primary(struct parser *parser)
{
	struct node_expression *expression = calloc(1,
			sizeof(struct node_expression));
	if (!expression) {
		fatal("failed to allocate expression node");
	}

	if (accept(parser, TOKEN_IDENTIFIER)) {
		expression->type = EXPRESSION_IDENTIFIER;
		expression->data.identifier = parse_identifier(parser);
	} else if (accept(parser, TOKEN_STRING)) {
		expression->type = EXPRESSION_STRING;
		expression->data.string = parse_string(parser);
	} else if (accept(parser, TOKEN_LBRACK)) {
		expression->type = EXPRESSION_ARRAY;
		expression->data.array = parse_array(parser);
	} else if (accept(parser, TOKEN_TRUE) || accept(parser, TOKEN_FALSE)) {
		expression->type = EXPRESSION_BOOL;
		expression->data.boolean = parse_bool(parser);
	} else {
		fatal("unexpected token %s", token_to_string(parser->cur));
	}

	return expression;
}

struct node_arguments *
parse_arguments(struct parser *parser)
{
	struct node_arguments *arguments = calloc(1,
			sizeof(struct node_arguments));
	assert(arguments);

	arguments->args = calloc(1, sizeof(struct node_expression_list));
	assert(arguments->args);
	arguments->kwargs = calloc(1, sizeof(struct node_keyword_list));
	assert(arguments->kwargs);

	for (;;) {
		while (accept(parser, TOKEN_EOL));

		if (accept(parser, TOKEN_RPAREN)) {
			break;
		}

		struct node_expression *expression = parse_expression(parser);
		if (accept(parser, TOKEN_COLON)) {
			keyword_list_appened(arguments->kwargs, expression,
					parse_expression(parser));
		} else {
			expression_list_appened(arguments->args, expression);
		}

		if (accept(parser, TOKEN_RPAREN)) {
			break;
		}

		expect(parser, TOKEN_COMMA);
	}

	return arguments;
}

static struct node_expression *
parse_function(struct parser *parser, struct node_expression *left)
{
	if (left->type != EXPRESSION_IDENTIFIER) {
		fatal("function must be called on an identifier");
	}

	struct node_expression *expression = calloc(1,
			sizeof(struct node_expression));
	assert(expression);

	expression->type = EXPRESSION_FUNCTION;
	expression->data.function = calloc(1, sizeof(struct node_function));
	assert(expression->data.function);

	expression->data.function->left = left;
	expression->data.function->right = parse_arguments(parser);

	return expression;
}

struct node_expression *
parse_method(struct parser *parser, struct node_expression *left)
{
	if (left->type != EXPRESSION_IDENTIFIER
			&& left->type != EXPRESSION_STRING) {
		fatal("method must be called on an identifier or a string");
	}

	struct node_expression *expression = calloc(1,
			sizeof(struct node_expression));
	assert(expression);

	expression->type = EXPRESSION_METHOD;
	expression->data.method = calloc(1, sizeof(struct node_method));
	assert(expression->data.method);

	expression->data.method->left = left;
	expression->data.method->right = parse_expression(parser);
	assert(expression->data.method->right->type == EXPRESSION_FUNCTION);

	return expression;
}

struct node_expression *
parse_postfix(struct parser *parser)
{
	struct node_expression *expression = parse_primary(parser);

	if (accept(parser, TOKEN_LPAREN)) {
		return parse_function(parser, expression);
	} else if (accept(parser, TOKEN_DOT)) {
		return parse_method(parser, expression);
	}

	return expression;
}

bool
is_assignment_op(struct parser *parser)
{
	static const enum token_type ops[] = {
		TOKEN_ASSIGN,
		TOKEN_STAREQ,
		TOKEN_SLASHEQ,
		TOKEN_MODEQ,
		TOKEN_PLUSEQ,
		TOKEN_MINEQ,
	};

	for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
		if (parser->cur->type == ops[i]) {
			return true;
		}
	}

	return false;
}

enum node_assignment_op
parse_assignment_op(struct parser *parser)
{
	if (accept(parser, TOKEN_ASSIGN)) {
		return ASSIGNMENT_ASSIGN;
	} else if (accept(parser, TOKEN_STAREQ)) {
		return ASSIGNMENT_STAREQ;
	} else if (accept(parser, TOKEN_SLASHEQ)) {
		return ASSIGNMENT_SLASHEQ;
	} else if (accept(parser, TOKEN_MODEQ)) {
		return ASSIGNMENT_MODEQ;
	} else if (accept(parser, TOKEN_PLUSEQ)) {
		return ASSIGNMENT_PLUSEQ;
	} else if (accept(parser, TOKEN_MINEQ)) {
		return ASSIGNMENT_MINEQ;
	} else {
		fatal("%s is not an assignment operation",
				token_to_string(parser->cur));
	}

	return -1;
}

struct node_expression *
parse_assignment(struct parser *parser, struct node_expression *left)
{
	struct node_expression *expression = calloc(1,
			sizeof(struct node_expression));
	assert(expression);

	expression->type = EXPRESSION_ASSIGNMENT;
	expression->data.assignment = calloc(1, sizeof(struct node_assignment));
	assert(expression->data.assignment);

	expression->data.assignment->left = left;
	expression->data.assignment->op = parse_assignment_op(parser);
	expression->data.assignment->right = parse_expression(parser);

	return expression;
}

struct node_expression *
parse_expression(struct parser *parser)
{
	//struct node_expression *left = parse_or(parser);
	struct node_expression *left = parse_postfix(parser);
	if (is_assignment_op(parser)) {
		return parse_assignment(parser, left);
	} else if (accept(parser, TOKEN_QM)) {
		fatal("todo condition expression");
	}

	return left;
}

struct node_statement *
parse_statement(struct parser *parser)
{
	struct node_statement *statement = calloc(1,
			sizeof(struct node_statement));
	if (!statement) {
		fatal("failed to allocate statement node");
	}

	while (accept(parser, TOKEN_EOL));

	if (accept(parser, TOKEN_EOF)) {
		free(statement);
		return NULL;
	} else if (accept(parser, TOKEN_FOREACH)) {
		statement->type = STATEMENT_ITERATION;
		fatal("TODO iteration statement");
	} else if (accept(parser, TOKEN_IF)) {
		statement->type = STATEMENT_SELECTION;
		fatal("TODO selection statement");
	} else {
		statement->type = STATEMENT_EXPRESSION;
		statement->data.expression = parse_expression(parser);
	}

	return statement;
}

struct node_root
parse(const char *source_dir)
{
	info("Source dir: %s", source_dir);

	char source_path[PATH_MAX] = {0};
	sprintf(source_path, "%s/%s", source_dir, "meson.build");

	struct parser parser = {0};
	lexer_init(&parser.lexer, source_path);
	parser.cur = lexer_tokenize(&parser.lexer);

	struct node_root root = { 0 };
	while (parser.cur->type != TOKEN_EOF) {
		// todo add statement to node_root
		root.statements = realloc(root.statements,
				++root.n * sizeof(struct node_statement *));
		root.statements[root.n - 1] = parse_statement(&parser);
	}

	lexer_finish(&parser.lexer);

	return root;
}
