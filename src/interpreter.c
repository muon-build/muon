#include "builtin.h"
#include "interpreter.h"
#include "parser.h"

#include <assert.h>
#include <stdio.h>

struct node *
get_node(struct ast *ast, uint32_t i)
{
	return darr_get(&ast->nodes, i);
}

#define BUF_SIZE 255

const char *
source_location(struct ast *ast, uint32_t id)
{
	struct node *n = get_node(ast, id);
	static char buf[BUF_SIZE + 1];

	snprintf(buf, BUF_SIZE, "line: %d, col: %d", n->tok->line, n->tok->col);

	return buf;
}

static bool
interp_function(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	return builtin_run(ast, wk, n, obj);
}

static bool
interp_node(struct ast *ast, struct workspace *wk, struct node *n)
{
	uint32_t obj;

	switch (n->type) {
	case node_function:
		return interp_function(ast, wk, n, &obj);
	case node_bool: break;
	case node_id: break;
	case node_number: break;
	case node_string: break;
	case node_format_string: break;
	case node_continue: break;
	case node_break: break;
	case node_argument: break;
	case node_array: break;
	case node_dict: break;
	case node_empty: break;
	case node_or: break;
	case node_and: break;
	case node_comparison: break;
	case node_arithmetic: break;
	case node_not: break;
	case node_index: break;
	case node_method: break;
	case node_assignment: break;
	case node_plus_assignment: break;
	case node_foreach_clause: break;
	case node_if: break;
	case node_if_clause: break;
	case node_u_minus: break;
	case node_ternary: break;
	case node_any: assert(false && "unreachable"); break;
	}

	return true;
}

bool
interpret(struct ast *ast, struct workspace *wk)
{
	uint32_t i;
	for (i = 0; i < ast->ast.len; ++i) {
		if (!interp_node(ast, wk, get_node(ast, *(uint32_t *)darr_get(&ast->ast, i)))) {
			return false;
		}
	}

	return true;
}
