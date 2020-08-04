#include "parse.h"
#include "ast.h"
#include "lexer.h"
#include "token.h"
#include "log.h"

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define PATH_MAX 4096

const char *
expression_type_to_string(enum node_expression_type type)
{
#define EXPR_TRANSLATE(e) case e: return #e;
	switch (type) {
	EXPR_TRANSLATE(EXPRESSION_NONE);
	EXPR_TRANSLATE(EXPRESSION_ASSIGNMENT);
	EXPR_TRANSLATE(EXPRESSION_CONDITION);
	EXPR_TRANSLATE(EXPRESSION_OR);
	EXPR_TRANSLATE(EXPRESSION_AND);
	EXPR_TRANSLATE(EXPRESSION_EQUALITY);
	EXPR_TRANSLATE(EXPRESSION_RELATION);
	EXPR_TRANSLATE(EXPRESSION_ADDITION);
	EXPR_TRANSLATE(EXPRESSION_MULTIPLICATION);
	EXPR_TRANSLATE(EXPRESSION_UNARY);
	EXPR_TRANSLATE(EXPRESSION_SUBSCRIPT);
	EXPR_TRANSLATE(EXPRESSION_FUNCTION);
	EXPR_TRANSLATE(EXPRESSION_METHOD);
	EXPR_TRANSLATE(EXPRESSION_IDENTIFIER);
	EXPR_TRANSLATE(EXPRESSION_STRING);
	EXPR_TRANSLATE(EXPRESSION_ARRAY);
	default:
		report("unknown token");
		break;
	}
#undef EXPR_TRANSLATE
	return "";
}

struct parser
{
	struct lexer lexer;
	struct token *cur;
	struct token *last;
};

struct node_expression *parse_expression(struct parser *);

static struct token *
consume(struct parser *parser)
{
	parser->last = parser->cur;
	parser->cur = lexer_tokenize(&parser->lexer);
	info("cur %s %s", token_to_string(parser->cur), (parser->cur->data ?
			parser->cur->data : ""));

	return parser->cur;
}

static void
expect(struct token *token, enum token_type type)
{
	if (token->type != type) {
		fatal("expected %s, got %s", token_type_to_string(type),
				token_to_string(token));
	}
}

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
	expect(parser->cur, TOKEN_IDENTIFIER);

	struct node_identifier *identifier = calloc(1,
			sizeof(struct node_identifier));
	if (!identifier) {
		fatal("failed to allocate identifier node");
	}

	identifier->data = calloc(parser->cur->n + 1, sizeof(char));
	strncpy(identifier->data, parser->cur->data, parser->cur->n);
	identifier->n = parser->cur->n;

	return identifier;
}

struct node_string *
parse_string(struct parser *parser)
{
	expect(parser->cur, TOKEN_STRING);

	struct node_string *string = calloc(1, sizeof(struct node_string));
	if (!string) {
		fatal("failed to allocate string node");
	}

	string->data = calloc(parser->cur->n + 1, sizeof(char));
	strncpy(string->data, parser->cur->data, parser->cur->n);
	string->n = parser->cur->n;

	return string;
}

struct node_expression_list *
parse_array(struct parser *parser)
{
	struct node_expression_list *list = calloc(1,
			sizeof(struct node_expression_list));
	if (!list) {
		fatal("failed to allocate array");
	}
	struct token *token = NULL;
	for (;;) {
		do {
		token = consume(parser);
		} while (token->type == TOKEN_EOL);
		if (token->type == TOKEN_RBRACK) {
			break;
		}
		expression_list_appened(list, parse_expression(parser));
		expect(parser->cur, TOKEN_COMMA);
	}

	return list;
}

struct node_expression *
parse_primary(struct parser *parser)
{
	struct node_expression *expression = calloc(1,
			sizeof(struct node_expression));
	if (!expression) {
		fatal("failed to allocate expression node");
	}

	switch(parser->cur->type) {
	case TOKEN_IDENTIFIER:
		expression->type = EXPRESSION_IDENTIFIER;
		expression->data.identifier = parse_identifier(parser);
		break;
	case TOKEN_STRING:
		expression->type = EXPRESSION_STRING;
		expression->data.string = parse_string(parser);
		break;
	case TOKEN_LBRACK:
		expression->type = EXPRESSION_ARRAY;
		expression->data.array = parse_array(parser);
		break;
	case TOKEN_NUMBER:
	case TOKEN_TRUE:
	case TOKEN_FALSE:
	case TOKEN_LPAREN:
		fatal("TODO primary %s", token_to_string(parser->cur));
		break;
	default:
		fatal("unexpected token %s", token_to_string(parser->cur));
	}

	return expression;
}

struct node_arguments *
parse_arguments(struct parser *parser)
{
	info("parse_arguments");
	expect(parser->cur, TOKEN_LPAREN);

	struct node_arguments *arguments = calloc(1,
			sizeof(struct node_arguments));
	if (!arguments) {
		fatal("failed to allocate arguments node");
	}

	arguments->args = calloc(1, sizeof(struct node_expression_list));
	if (!arguments->args) {
		fatal("failed to allocate argument list");
	}

	arguments->kwargs = calloc(1, sizeof(struct node_keyword_list));
	if (!arguments->kwargs) {
		fatal("failed to allocate keyword argument list");
	}

	struct token *token = NULL;
	for (;;) {
		do {
			token = consume(parser);
		} while (token->type == TOKEN_EOL);

		if (token->type == TOKEN_RPAREN) {
			consume(parser);
			break;
		}

		struct node_expression *expression = parse_expression(parser);
		if (expression->type == EXPRESSION_IDENTIFIER) {
			token = consume(parser);
			keyword_list_appened(arguments->kwargs, expression,
					parse_expression(parser));
		} else {
			expression_list_appened(arguments->args, expression);
		}

		if (parser->cur->type == TOKEN_RPAREN) {
			consume(parser);
			break;
		}

		expect(parser->cur, TOKEN_COMMA);
	}
	info("parse_arguments done");

	return arguments;
}

struct node_function *
parse_function(struct parser *parser, struct node_expression *expression)
{
	info("parse_function");

	if (expression->type != EXPRESSION_IDENTIFIER) {
		info("function on %s",
				expression_type_to_string(expression->type));
		fatal("function must be called on an identifier");
	}

	struct node_function *function = calloc(1,
			sizeof(struct node_function));
	if (!function) {
		fatal("failed to allocate function node");
	}

	function->left = expression->data.identifier;
	function->right = parse_arguments(parser);

	info("parse_function done");
	return function;
}

struct node_method *
parse_method(struct parser *parser, struct node_expression *expression)
{
	info("parse_method");
	if (expression->type != EXPRESSION_IDENTIFIER
		&& expression->type != EXPRESSION_STRING) {
		fatal("method must be called on an identifier or a string");
	}

	struct node_method *method = calloc(1, sizeof(struct node_method));
	if (!method) {
		fatal("failed to allocate function node");
	}

	method->left = expression;
	consume(parser);
	struct node_expression *right = parse_expression(parser);
	if (right->type != EXPRESSION_FUNCTION) {
		fatal("left part of a method must be a function");
	}
	method->right = right->data.function;
	free(right);

	info("parse_method done");
	return method;
}

struct node_expression *
parse_postfix_expression(struct parser *parser)
{
	info("parse_postfix_expression");
	struct node_expression *expression = parse_primary(parser);

	struct token *token = consume(parser);
	switch (token->type) {
	case TOKEN_LPAREN:
		expression->data.function = parse_function(parser, expression);
		expression->type = EXPRESSION_FUNCTION;
		break;
	case TOKEN_DOT:
		expression->data.method = parse_method(parser, expression);
		expression->type = EXPRESSION_METHOD;
		break;
	default:
		break;
	}

	info("parse_postfix_expression done");
	return expression;
}

/*
bool
is_assignment_op(struct token *token)
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
		if (token->type == ops[i]) {
			return true;
		}
	}

	return false;
}
*/

struct node_expression *
parse_expression(struct parser *parser)
{
	while(parser->cur->type == TOKEN_EOL) {
		consume(parser);
	}
	info("parse_expression");

	struct node_expression *left = parse_postfix_expression(parser);
	info("parse_expression done");
	return left;
}

struct node_statement *
parse_statement(struct parser *parser)
{
	info("parse_statement");
	struct node_statement *statement = calloc(1,
			sizeof(struct node_statement));
	if (!statement) {
		fatal("failed to allocate statement node");
	}

	struct token *token = consume(parser);
	switch (token->type) {
	case TOKEN_FOREACH:
		statement->type = STATEMENT_ITERATION;
		fatal("TODO iteration statement");
		break;
	case TOKEN_IF:
		statement->type = STATEMENT_SELECTION;
		fatal("TODO selection statement");
		break;
	case TOKEN_EOF:
		info("EOF");
		free(statement);
		return NULL;
	default:
		statement->type = STATEMENT_EXPRESSION;
		statement->data.expression = parse_expression(parser);
	};

	return statement;
}

struct node_root
parse(const char *source_dir)
{
	info("parse");
	char source_path[PATH_MAX] = {0};
	sprintf(source_path, "%s/%s", source_dir, "meson.build");

	struct parser parser = {0};
	lexer_init(&parser.lexer, source_path);
	struct node_root root = { 0 };
	do {
		// todo add statement to node_root
		parse_statement(&parser);
	} while (parser.cur->type != TOKEN_EOF);

	lexer_finish(&parser.lexer);

	return root;
}
