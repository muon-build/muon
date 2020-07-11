#include "parse.h"
#include "lexer.h"
#include "token.h"
#include "log.h"

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_MAX 4096

struct parser
{
	struct lexer lexer;
	struct token *cur;
	struct token *last;
};

static struct token *
consume(struct parser *parser)
{
	parser->last = parser->cur;
	parser->cur = lexer_tokenize(&parser->lexer);
	return parser->cur;
}

static void
expect(struct parser *parser, enum token_type type)
{
	if (parser->cur->type != type) {
		fatal("error: expected %s, got %s", token_type_to_string(type),
				token_to_string(parser->cur));
	}
}

static const char *
is_function(struct token *token)
{
	if (token->type != TOKEN_IDENTIFIER) {
		return NULL;
	}

	/* Keep in order */
	static const char *funcs[] = {
		"project",
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

static struct node *
parse_function(struct parser *parser, const char *func)
{
	info("parse_function");
	struct node *function = calloc(1, sizeof(struct node));
	if (!function) {
		fatal("parser: failed to allocate function node");
	}

	consume(parser);
	expect(parser, TOKEN_LPAREN);

	return function;
}

static struct node *
parse_expression(struct parser *parser)
{
	info("parse_expression");
	expect(parser, TOKEN_IDENTIFIER);

	const char *func = is_function(parser->cur);
	if (func) {
		return parse_function(parser, func);
	}

	return NULL;
}

static struct node *
parse_statement(struct parser *parser)
{
	info("parse_statement");
	struct node *node = NULL;
	switch(parser->cur->type) {
	case TOKEN_IDENTIFIER:
		node = parse_expression(parser);
		break;
/*
	case TOKEN_IF:
		node = parse_selection_statement(parser);
		break;
	case TOKEN_FOREACH:
		node = parse_iteration_statement;
		break;
*/
	default:
		fatal("error: unexpected token %s", token_to_string(parser->cur));
		break;
	}
	return node;
}

struct node *
parse(const char *source_dir)
{
	char source_path[PATH_MAX] = {0};
	sprintf(source_path, "%s/%s", source_dir, "meson.build");

	struct parser parser = {0};
	lexer_init(&parser.lexer, source_path);
	struct node *root = NULL;
	while(consume(&parser)->type != TOKEN_EOF) {
		parse_statement(&parser);
	}

	lexer_finish(&parser.lexer);

	return root;
}
