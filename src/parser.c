#include "posix.h"

#include "lexer.h"
#include "log.h"
#include "parser.h"

#define PATH_MAX 4096
#define NODE_MAX_CHILDREN 3
#define BUF_SIZE 255

const uint32_t arithmetic_type_count = 5;

static const char *node_name[] = {
	[node_bool] = "bool",
	[node_id] = "id",
	[node_number] = "number",
	[node_string] = "string",
	[node_format_string] = "format_string",
	[node_continue] = "continue",
	[node_break] = "break",
	[node_argument] = "argument",
	[node_array] = "array",
	[node_dict] = "dict",
	[node_empty] = "empty",
	[node_or] = "or",
	[node_and] = "and",
	[node_comparison] = "comparison",
	[node_arithmetic] = "arithmetic",
	[node_not] = "not",
	[node_index] = "index",
	[node_method] = "method",
	[node_function] = "function",
	[node_assignment] = "assignment",
	[node_plus_assignment] = "plus_assignment",
	[node_foreach_clause] = "foreach_clause",
	[node_if] = "if",
	[node_if_clause] = "if_clause",
	[node_u_minus] = "u_minus",
	[node_ternary] = "ternary",
};

struct parser {
	struct lexer lexer;
	struct token *last_last, *last;
	struct ast *ast;
	uint32_t token_i;
};

const char *
source_location(struct ast *ast, uint32_t id)
{
	struct node *n = get_node(ast, id);
	static char buf[BUF_SIZE + 1];

	snprintf(buf, BUF_SIZE, "line: %d, col: %d", n->tok->line, n->tok->col);

	return buf;
}

static struct token *
next_tok(struct parser *p)
{
	p->last_last = p->last;
	p->last = darr_get(&p->lexer.tok, p->token_i);
	++p->token_i;
	if (p->token_i >= p->lexer.tok.len) {
		p->token_i = p->lexer.tok.len - 1;
	}

	return darr_get(&p->lexer.tok, p->token_i);
}

static bool
accept(struct parser *p, enum token_type type)
{
	if (p->last->type == type) {
		next_tok(p);
		return true;
	}

	return false;
}

static bool
expect(struct parser *p, enum token_type type)
{
	if (!accept(p, type)) {
		LOG_W(log_parse, "expecting token %s, got token %s",
			token_type_to_string(type), token_to_string(p->last));
		return false;
	}

	return true;
}

struct node *
get_node(struct ast *ast, uint32_t i)
{
	return darr_get(&ast->nodes, i);
}

static struct node *
make_node(struct parser *p, uint32_t *idx, enum node_type t)
{
	*idx = darr_push(&p->ast->nodes, &(struct node){ .type = t });
	return darr_get(&p->ast->nodes, *idx);
}

static uint32_t
get_child(struct ast *ast, uint32_t idx, uint32_t c)
{
	struct node *n = get_node(ast, idx);
	assert(c < NODE_MAX_CHILDREN);
	enum node_child_flag chflg = 1 << c;
	assert(chflg & n->chflg);
	switch (chflg) {
	case node_child_l:
		return n->l;
	case node_child_r:
		return n->r;
	case node_child_c:
		return n->c;
	case node_child_d:
		return n->d;
	}

	assert(false && "unreachable");
	return 0;
}

static void
add_child(struct parser *p, uint32_t parent, enum node_child_flag chflg, uint32_t c_id)
{
	struct node *n = darr_get(&p->ast->nodes, parent);
	assert(!(chflg & n->chflg) && "you tried to set the same child more than once");
	n->chflg |= chflg;

	switch (chflg) {
	case node_child_l:
		n->l = c_id;
		break;
	case node_child_r:
		n->r = c_id;
		break;
	case node_child_c:
		n->c = c_id;
		break;
	case node_child_d:
		n->d = c_id;
		break;
	}
}

const char *
node_type_to_s(enum node_type t)
{
	return node_name[t];
}


const char *
node_to_s(struct node *n)
{
	static char buf[BUF_SIZE + 1];
	uint32_t i = 0;

	i += snprintf(&buf[i], BUF_SIZE - i, "%s", node_name[n->type]);

	switch (n->type) {
	case node_id:
	case node_number:
	case node_string:
		i += snprintf(&buf[i], BUF_SIZE - i, ":%s", n->tok->data);
		break;
	case node_argument:
		i += snprintf(&buf[i], BUF_SIZE - i, ":%s", n->data == arg_kwarg ? "kwarg" : "normal");
		break;
	default:
		break;
	}

	return buf;
}

void
print_tree(struct ast *ast, uint32_t id, uint32_t d)
{
	struct node *n = get_node(ast, id);
	uint32_t i;

	for (i = 0; i < d; ++i) {
		putc(' ', stdout);
	}

	printf("node: %d, %s\n", id, node_to_s(n));

	for (i = 0; i < NODE_MAX_CHILDREN; ++i) {
		if ((1 << i) & n->chflg) {
			print_tree(ast, get_child(ast, id, i), d + 1);
		}
	}
}

typedef bool (*parse_func)(struct parser *, uint32_t *);
static bool parse_stmt(struct parser *p, uint32_t *id);

static bool
parse_args(struct parser *p, uint32_t *id)
{
	uint32_t s_id, c_id, v_id;
	enum arg_type at = arg_normal;
	struct node *n;

	if (!parse_stmt(p, &s_id)) {
		return false;
	}

	if (get_node(p->ast, s_id)->type == node_empty) {
		*id = s_id;
		return true;
	}

	if (accept(p, tok_colon)) {
		at = arg_kwarg;

		if (get_node(p->ast, s_id)->type != node_id) {
			LOG_W(log_parse, "Dictionary key must be a plain identifier.");
			return false;
		}

		if (!parse_stmt(p, &v_id)) {
			return false;
		}

		if (!accept(p, tok_comma)) {
			n = make_node(p, id, node_argument);
			n->data = at;

			add_child(p, *id, node_child_l, s_id);
			add_child(p, *id, node_child_r, v_id);
			return true;
		}
	} else if (!accept(p, tok_comma)) {
		n = make_node(p, id, node_argument);
		n->data = at;

		add_child(p, *id, node_child_l, s_id);
		return true;
	}

	if (!parse_args(p, &c_id)) {
		return false;
	}

	n = make_node(p, id, node_argument);
	n->data = at;

	add_child(p, *id, node_child_l, s_id);
	if (at == arg_kwarg) {
		add_child(p, *id, node_child_r, v_id);
	}
	add_child(p, *id, node_child_c, c_id);

	return true;
}

static bool
parse_key_values(struct parser *p, uint32_t *id)
{
	uint32_t s_id, v_id, c_id;
	struct node *n;

	if (!parse_stmt(p, &s_id)) {
		return false;
	}

	if (get_node(p->ast, s_id)->type == node_empty) {
		*id = s_id;
		return true;
	}

	if (!accept(p, tok_colon)) {
		LOG_W(log_parse, "missing colon");
		return false;
	}

	if (!parse_stmt(p, &v_id)) {
		return false;
	}

	if (!accept(p, tok_comma)) {
		n = make_node(p, id, node_argument);
		n->data = arg_kwarg;

		add_child(p, *id, node_child_l, s_id);
		add_child(p, *id, node_child_r, v_id);
		return true;
	}

	if (!parse_key_values(p, &c_id)) {
		return false;
	}

	n = make_node(p, id, node_argument);
	n->data = arg_kwarg;

	add_child(p, *id, node_child_l, s_id);
	add_child(p, *id, node_child_r, v_id);
	add_child(p, *id, node_child_c, c_id);

	return true;
}

static bool
parse_index_call(struct parser *p, uint32_t *id, uint32_t l_id)
{
	return false;
}

static bool
parse_e9(struct parser *p, uint32_t *id)
{
	struct node *n;

	if (accept(p, tok_true)) {
		n = make_node(p, id, node_bool);
		n->data = 1;
	} else if (accept(p, tok_false)) {
		n = make_node(p, id, node_bool);
		n->data = 0;
	} else if (accept(p, tok_identifier)) {
		n = make_node(p, id, node_id);
		n->tok = p->last_last;
	} else if (accept(p, tok_number)) {
		n = make_node(p, id, node_number);
		n->tok = p->last_last;
	} else if (accept(p, tok_string)) {
		n = make_node(p, id, node_string);
		n->tok = p->last_last;
	} else {
		make_node(p, id, node_empty);
	}

	return true;
}

static bool
parse_method_call(struct parser *p, uint32_t *id, uint32_t l_id)
{
	uint32_t meth_id, args, c_id = 0;
	bool have_c = false;

	if (!parse_e9(p, &meth_id)) {
		return false;
	} else if (!expect(p, tok_lparen)) {
		return false;
	} else if (!parse_args(p, &args)) {
		return false;
	} else if (!expect(p, tok_rparen)) {
		return false;
	}

	if (accept(p, tok_dot)) {
		have_c = true;

		if (!parse_method_call(p, &c_id, l_id)) {
			return false;
		}
	}

	struct node *n;
	n = make_node(p, id, node_method);
	n->data = have_c;

	add_child(p, *id, node_child_l, l_id);
	add_child(p, *id, node_child_r, meth_id);
	add_child(p, *id, node_child_c, args);

	if (have_c) {
		add_child(p, *id, node_child_d, c_id);
	}

	return true;
}

static bool
parse_e8(struct parser *p, uint32_t *id)
{
	uint32_t v;

	if (accept(p, tok_lparen)) {
		return parse_stmt(p, id);
	} else if (accept(p, tok_lbrack)) {
		if (!parse_args(p, &v)) {
			return false;
		}

		if (!expect(p, tok_rbrack)) {
			return false;
		}

		make_node(p, id, node_array);
		add_child(p, *id, node_child_l, v);
	} else if (accept(p, tok_lcurl)) {
		if (!parse_key_values(p, &v)) {
			return false;
		}

		if (!expect(p, tok_rcurl)) {
			return false;
		}

		make_node(p, id, node_dict);
		add_child(p, *id, node_child_l, v);
	} else {
		return parse_e9(p, id);
	}

	return true;
}

static bool
parse_e7(struct parser *p, uint32_t *id)
{
	uint32_t p_id, l_id;
	if (!(parse_e8(p, &l_id))) {
		return false;
	}

	if (accept(p, tok_lparen)) {
		uint32_t args;

		if (!parse_args(p, &args)) {
			return false;
		} else if (!expect(p, tok_rparen)) {
			return false;
		}

		if (get_node(p->ast, l_id)->type != node_id) {
			LOG_W(log_parse, "Function call must be applied to plain id");
			return false;
		}

		make_node(p, &p_id, node_function);
		add_child(p, p_id, node_child_l, l_id);
		add_child(p, p_id, node_child_r, args);
		l_id = p_id;
	}

	bool loop = true;
	while (loop) {
		loop = false;
		if (accept(p, tok_dot)) {
			loop = true;
			if (!parse_method_call(p, &p_id, l_id)) {
				return false;
			}
			l_id = p_id;
		}

		if (accept(p, tok_lbrack)) {
			loop = true;
			if (!parse_index_call(p, &p_id, l_id)) {
				return false;
			}
			l_id = p_id;
		}
	}

	*id = l_id;
	return true;
}

static bool
parse_e6(struct parser *p, uint32_t *id)
{
	uint32_t l_id;
	if (!(parse_e7(p, &l_id))) {
		return false;
	}

	if (accept(p, tok_not)) {
		make_node(p, id, node_not);
		add_child(p, *id, node_child_l, l_id);
	} else if (accept(p, tok_minus)) {
		make_node(p, id, node_u_minus);
		add_child(p, *id, node_child_r, l_id);
	} else {
		*id = l_id;
	}

	return true;
}

static bool
parse_arith(struct parser *p, uint32_t *id, parse_func parse_upper,
	uint32_t map_start, uint32_t map_end)
{
	static enum token_type map[] = {
		[arith_add] = tok_plus,
		[arith_sub] = tok_minus,
		[arith_mod] = tok_modulo,
		[arith_mul] = tok_star,
		[arith_div] = tok_slash,
	};
	struct node *n;

	uint32_t i, p_id, l_id, r_id;
	if (!(parse_upper(p, &l_id))) {
		return false;
	}

	while (true) {
		for (i = map_start; i < map_end; ++i) {
			if (accept(p, map[i])) {
				n = make_node(p, &p_id, node_arithmetic);
				n->data = i;

				if (!(parse_upper(p, &r_id))) {
					return false;
				}

				add_child(p, p_id, node_child_l, l_id);
				l_id = p_id;
			}
		}

		if (i == map_end) {
			break;
		}
	}

	*id = l_id;
	return true;
}

static bool
parse_e5muldiv(struct parser *p, uint32_t *id)
{
	return parse_arith(p, id, parse_e6, arith_mod, arithmetic_type_count);
}

static bool
parse_e5addsub(struct parser *p, uint32_t *id)
{
	return parse_arith(p, id, parse_e5muldiv, 0, arith_mod);
}

static bool
make_comparison_node(struct parser *p, uint32_t *id, uint32_t l_id, enum comparison_type comp)
{
	uint32_t r_id;
	struct node *n = make_node(p, id, node_comparison);
	n->data = comp;

	if (!(parse_e5addsub(p, &r_id))) {
		return false;
	}

	add_child(p, *id, node_child_l, l_id);
	add_child(p, *id, node_child_r, r_id);

	return true;
}

static bool
parse_e4(struct parser *p, uint32_t *id)
{
	static enum token_type map[] = {
		[comp_equal] = tok_eq,
		[comp_nequal] = tok_neq,
		[comp_lt] = tok_lt,
		[comp_le] = tok_leq,
		[comp_gt] = tok_gt,
		[comp_ge] = tok_geq,
		[comp_in] = tok_in,
	};

	uint32_t i, l_id;
	if (!(parse_e5addsub(p, &l_id))) {
		return false;
	}

	for (i = 0; i < comp_not_in; ++i) {
		if (accept(p, map[i])) {
			return make_comparison_node(p, id, l_id, i);
		}
	}

	if (accept(p, tok_not) && accept(p, tok_in)) {
		return make_comparison_node(p, id, l_id, comp_not_in);
	}

	*id = l_id;
	return true;
}

static bool
parse_e3(struct parser *p, uint32_t *id)
{
	uint32_t p_id, l_id, r_id;
	if (!(parse_e4(p, &l_id))) {
		return false;
	}

	while (accept(p, tok_or)) {
		make_node(p, &p_id, node_and);
		if (!parse_e4(p, &r_id)) {
			return false;
		}

		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, r_id);

		l_id = p_id;
	}

	*id = l_id;
	return true;
}

static bool
parse_e2(struct parser *p, uint32_t *id)
{
	uint32_t p_id, l_id, r_id;
	if (!(parse_e3(p, &l_id))) {
		return false;
	}

	while (accept(p, tok_or)) {
		make_node(p, &p_id, node_or);
		if (!parse_e3(p, &r_id)) {
			return false;
		}

		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, r_id);

		l_id = p_id;
	}

	*id = l_id;
	return true;
}

static bool
parse_stmt(struct parser *p, uint32_t *id)
{
	uint32_t l_id = 0; // compiler thinks this won't get initialized...
	if (!(parse_e2(p, &l_id))) {
		return false;
	}

	if (accept(p, tok_plus)) {
		uint32_t v;

		if (get_node(p->ast, l_id)->type != node_id) {
			LOG_W(log_parse, "Plusassignment target must be an id.");
			return false;
		} else if (!parse_stmt(p, &v)) {
			return false;
		}

		make_node(p, id, node_plus_assignment);
		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, v);
	} else if (accept(p, tok_assign)) {
		uint32_t v;

		if (get_node(p->ast, l_id)->type != node_id) {
			LOG_W(log_parse, "assignment target must be an id.");
			return false;
		} else if (!parse_stmt(p, &v)) {
			return false;
		}

		make_node(p, id, node_assignment);
		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, v);
	} else if (accept(p, tok_question_mark)) {
		uint32_t a, b;

		if (!(parse_stmt(p, &a))) {
			return false;
		} else if (!(parse_stmt(p, &b))) {
			return false;
		}

		make_node(p, id, node_ternary);
		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, a);
		add_child(p, *id, node_child_c, b);
	} else {
		*id = l_id;
	}

	return true;
}

bool
parse(struct ast *ast, const char *source_dir)
{
	LOG_W(log_misc, "Source dir: %s", source_dir);

	char source_path[PATH_MAX] = { 0 };
	snprintf(source_path, PATH_MAX, "%s/%s", source_dir, "meson.build");

	struct parser parser = { .ast = ast };
	uint32_t id;

	darr_init(&parser.ast->nodes, sizeof(struct node));
	darr_init(&ast->ast, sizeof(uint32_t));

	if (!lexer_init(&parser.lexer, source_path)) {
		return false;
	} else if (!lexer_tokenize(&parser.lexer)) {
		goto err;
	}

	next_tok(&parser);

	bool loop = true;
	while (loop) {
		if (!(parse_stmt(&parser, &id))) {
			LOG_W(log_misc, "unexpected token %s while parsing '%s'",
				token_to_string(parser.last), source_path);
			goto err;
		}

		if (get_node(parser.ast, id)->type != node_empty) {
			darr_push(&ast->ast, &id);
		}

		loop = accept(&parser, tok_eol);
	}

	if (!expect(&parser, tok_eof)) {
		goto err;
	}


	/* lexer_finish(&parser.lexer); */
	return true;
err:
	/* lexer_finish(&parser.lexer); */
	return false;
}
