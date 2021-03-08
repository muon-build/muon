#include "eval.h"
#include "parser.h"
#include "function.h"
#include "options.h"
#include "ast.h"
#include "hash_table.h"
#include "log.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>

/*
enum variable_type {
	VARIABLE_STRING = 0 << 0,
	VARIABLE_NUMBER = 0 << 1,
	VARIABLE_BOOLEAN = 0 << 2,
};

struct variable {
	enum variable_type type;
	union {
		int number;
		char *string;
		bool boolean;
	} data;
};

static struct hash_table *variables = NULL;

struct variable *
get_variable_value(const char *name)
{
	struct variable *var = hash_table_get(variables, name);
	if (!var) {
		fatal("unknown variable '%s'", name);
	}

	return var;
}
*/
static void
eval_function(struct environment *env, struct ast_function *expr)
{
	info("eval_function %s", expr->left->data);
	function func = get_function(expr->left->data);
	if (!func) {
		fatal("failed to execute function '%s'", expr->left->data);
	}
	func(env, expr->right);
}

static void
eval_expression(struct environment *env, struct ast_expression *expression)
{
	info("expression %s", ast_expression_to_str(expression));
	switch(expression->type) {
	case EXPRESSION_FUNCTION:
		eval_function(env, expression->data.function);
		break;
	default:
		break;
		//fatal("todo handle expression %s",
		//		expr_to_str(expression->type));
	}
}

static void
check_first(struct ast_statement *statement)
{
	if (statement->type != STATEMENT_EXPRESSION) {
		goto check_first_err;
	}

	struct ast_expression *expr = statement->data.expression;
	if (expr->type != EXPRESSION_FUNCTION) {
		goto check_first_err;
	}

	if (strcmp(expr->data.function->left->data, "project") != 0) {
		goto check_first_err;
	}

	return;
check_first_err:
	fatal("first statement must be a call to function 'project'");
	return;
}

struct environment
eval(struct ast_root *root)
{
	//variables = hash_table_create(8);
	struct environment env = {0};

	env.options = options_create();

	check_first(root->statements[0]);

	for (size_t i = 0; i < root->n - 1; ++i) {
		struct ast_statement *statement = root->statements[i];

		switch(statement->type) {
		case STATEMENT_EXPRESSION:
			eval_expression(&env, statement->data.expression);
			break;
		case STATEMENT_SELECTION:
		case STATEMENT_ITERATION:
		default:
			fatal("unknown statement");
		}
	}

	return env;
}
