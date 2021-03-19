#include "interpreter.h"
#include "parser.h"
#include "options.h"
#include "ast.h"
#include "hash_table.h"
#include "log.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

/*
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

struct object *
eval_string(struct ast_string *string)
{
	struct object *obj = calloc(1, sizeof(struct object));
	if (!obj) {
		fatal("failed to allocate string object");
	}

	obj->string.data = calloc(string->n, sizeof(char));
	strncpy(obj->string.data, string->data, string->n);
	obj->string.n = string->n;

	obj->type = OBJECT_TYPE_STRING;
	return obj;
}

static void
string_format(struct context *ctx, struct object *object,
		struct ast_arguments *arguments)
{
	if (arguments->kwargs->n != 0) {
		fatal("string format doesn't support kwargs");
	}

	char *fmt = object->string.data;
	for(size_t i = 0; i < arguments->args->n; ++i) {
		struct object *arg = eval_expression(ctx,
				arguments->args->expressions[i]);
		const char *str_arg = object_to_str(arg);

		char needle[8] = {0};
		sprintf(needle, "@%zu@", i);

		const char *found = strstr(fmt, needle);
		if (!found) {
			fatal("%s not found\n", needle);
		}
		const ptrdiff_t idx = found - fmt;

		// new size of out
		const size_t s = (strlen(fmt) - strlen(needle))
				+ strlen(str_arg) + 1;
		char *out = calloc(s + 1, sizeof(char));

		// copy until needle
		strncpy(out, fmt, idx);

		// copy argument
		strcat(out, str_arg);

		// copy remaining
		strcat(out, fmt + idx + strlen(needle));

		fmt = out;
	}
	object->string.data = fmt;
	object->string.n = strlen(fmt);
}

static struct object *
eval_string_method(struct context *ctx, struct ast_method *method)
{
	struct object *obj = eval_expression(ctx, method->left);

	struct ast_function *func = method->right;
	if (strcmp(func->left->data, "format") == 0) {
		string_format(ctx, obj, func->right);
	} else {
		fatal("TODO string method %s", func->left->data);
	}

	return obj;
}

static struct object *
eval_identifier_method(struct context *ctx, struct ast_method *method)
{
	struct ast_identifier *id = method->left->data.identifier;
	struct object *obj = NULL;
	if (strcmp(id->data, "meson") == 0) {
		obj = eval_meson_object(ctx, method->right);
	} else {
		fatal("todo identifier method");
	}
	return obj;
}

struct object *
eval_method(struct context *ctx, struct ast_method *method)
{
	struct object *obj = NULL;
	struct ast_expression *expr = method->left;
	switch (expr->type) {
	case EXPRESSION_STRING:
		obj = eval_string_method(ctx, method);
		break;
	case EXPRESSION_IDENTIFIER:
		obj = eval_identifier_method(ctx, method);
		break;
	default:
		fatal("todo method %s", ast_expression_to_str(expr));
		break;
	}

	return obj;
}

static struct object *
eval_assignment(struct context *ctx, struct ast_assignment *assignment)
{
	struct object *obj = eval_expression(ctx, assignment->right);
	obj->id = assignment->left->data;

	void **value = hash_table_put(ctx->env, obj->id);

	switch (assignment->op) {
	case ASSIGNMENT_ASSIGN:
		*value = obj;
		break;
	default:
		fatal("todo assignment");
	}

	return NULL;
}

static struct object *
eval_identifier(struct context *ctx, struct ast_identifier *identifier)
{
	return hash_table_get(ctx->env, identifier->data);
}

static struct object *
eval_array(struct context *ctx, struct ast_expression_list *array)
{
	struct object *obj = calloc(1, sizeof(struct object));
	if (!obj) {
		fatal("failed to allocate array");
	}

	obj->type = OBJECT_TYPE_ARRAY;
	obj->array.n = array->n;
	obj->array.objects = calloc(array->n, sizeof(struct object*));

	for (size_t i = 0; i < array->n; ++i) {
		struct object *item = eval_expression(ctx, array->expressions[i]);
		if (!item) {
			fatal("array item at %zu is empty", i);
		}
		obj->array.objects[i] = item;
	}

	return obj;
}

struct object *
eval_expression(struct context *ctx, struct ast_expression *expression)
{
	struct object *obj = NULL;
	switch (expression->type) {
	case EXPRESSION_FUNCTION:
		obj = eval_function(ctx, expression->data.function);
		break;
	case EXPRESSION_METHOD:
		obj = eval_method(ctx, expression->data.method);
		break;
	case EXPRESSION_STRING:
		obj = eval_string(expression->data.string);
		break;
	case EXPRESSION_ASSIGNMENT:
		obj = eval_assignment(ctx, expression->data.assignment);
		break;
	case EXPRESSION_IDENTIFIER:
		obj = eval_identifier(ctx, expression->data.identifier);
		break;
	case EXPRESSION_ARRAY:
		obj = eval_array(ctx, expression->data.array);
		break;
	default:
		fatal("todo handle expression %s",
				ast_expression_to_str(expression));
		break;
	}

	return obj;
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

struct context
interpret_ast(struct ast_root *root)
{
	struct context ctx = {0};

	ctx.options = options_create();
	ctx.env = hash_table_create(8u);

	check_first(root->statements[0]);

	for (size_t i = 0; i < root->n - 1; ++i) {
		struct ast_statement *statement = root->statements[i];

		switch(statement->type) {
		case STATEMENT_EXPRESSION:
			eval_expression(&ctx, statement->data.expression);
			break;
		case STATEMENT_SELECTION:
		case STATEMENT_ITERATION:
		default:
			fatal("unknown statement");
		}
	}

	return ctx;
}
