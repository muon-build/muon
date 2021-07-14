#include "posix.h"

#include <stdarg.h>

#include "eval.h"
#include "lexer.h"
#include "log.h"
#include "parser.h"

#define PATH_MAX 4096
#define NODE_MAX_CHILDREN 4
#define BUF_SIZE 255

const uint32_t arithmetic_type_count = 5;

struct parser {
	struct source *src;
	struct tokens *toks;
	struct token *last_last, *last;
	struct ast *ast;
	uint32_t token_i;
};

const char *
node_type_to_s(enum node_type t)
{
	switch (t) {
	case node_null: return "null";
	case node_bool: return "bool";
	case node_id: return "id";
	case node_number: return "number";
	case node_string: return "string";
	case node_format_string: return "format_string";
	case node_continue: return "continue";
	case node_break: return "break";
	case node_argument: return "argument";
	case node_array: return "array";
	case node_dict: return "dict";
	case node_empty: return "empty";
	case node_or: return "or";
	case node_and: return "and";
	case node_comparison: return "comparison";
	case node_arithmetic: return "arithmetic";
	case node_not: return "not";
	case node_index: return "index";
	case node_method: return "method";
	case node_function: return "function";
	case node_assignment: return "assignment";
	case node_foreach_args: return "foreach_args";
	case node_foreach: return "foreach";
	case node_if: return "if";
	case node_u_minus: return "u_minus";
	case node_ternary: return "ternary";
	case node_block: return "block";
	}

	assert(false && "unreachable");
	return "";
}

__attribute__ ((format(printf, 2, 3)))
static void
parse_error(struct parser *p, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	error_message(p->src, p->last->line, p->last->col, fmt, args);
	va_end(args);
}

const char *
source_location(struct ast *ast, uint32_t id)
{
	struct node *n = get_node(ast, id);
	static char buf[BUF_SIZE + 1];

	snprintf(buf, BUF_SIZE, "line: %d, col: %d", n->line, n->col);

	return buf;
}

static struct token *
get_next_tok(struct parser *p)
{
	p->last_last = p->last;
	p->last = darr_get(&p->toks->tok, p->token_i);
	++p->token_i;
	if (p->token_i >= p->toks->tok.len) {
		p->token_i = p->toks->tok.len - 1;
	}

	return darr_get(&p->toks->tok, p->token_i);
}

static bool
accept(struct parser *p, enum token_type type)
{
	if (p->last->type == type) {
		get_next_tok(p);
		return true;
	}

	return false;
}

static bool
expect(struct parser *p, enum token_type type)
{
	if (!accept(p, type)) {
		parse_error(p, "expected '%s', got '%s'", tok_type_to_s(type), tok_type_to_s(p->last->type));
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
	struct node *n = darr_get(&p->ast->nodes, *idx);

	if (p->last_last) {
		n->line = p->last_last->line;
		n->col = p->last_last->col;
		n->dat = p->last_last->dat;
	}
	return n;
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
node_to_s(struct node *n)
{
	static char buf[BUF_SIZE + 1];
	uint32_t i = 0;

	i += snprintf(&buf[i], BUF_SIZE - i, "%s", node_type_to_s(n->type));

	switch (n->type) {
	case node_id:
		i += snprintf(&buf[i], BUF_SIZE - i, ":%s", n->dat.s);
		break;
	case node_string:
		i += snprintf(&buf[i], BUF_SIZE - i, ":'%s'", n->dat.s);
		break;
	case node_number:
		i += snprintf(&buf[i], BUF_SIZE - i, ":%ld", n->dat.n);
		break;
	case node_argument:
		i += snprintf(&buf[i], BUF_SIZE - i, ":%s", n->subtype == arg_kwarg ? "kwarg" : "normal");
		break;
	case node_if:
		i += snprintf(&buf[i], BUF_SIZE - i, ":%s", n->subtype == if_normal ? "normal" : "else");
		break;
	case node_arithmetic: {
		const char *s;
		switch ((enum arithmetic_type)n->subtype) {
		case arith_add:
			s = "+";
			break;
		case arith_sub:
			s = "-";
			break;
		case arith_mod:
			s = "%";
			break;
		case arith_mul:
			s = "*";
			break;
		case arith_div:
			s = "/";
			break;
		default:
			assert(false);
			return NULL;
		}

		i += snprintf(&buf[i], BUF_SIZE - i, " op '%s'", s);
		break;
	}
	default:
		break;
	}

	return buf;
}

typedef bool (*parse_func)(struct parser *, uint32_t *);
static bool parse_stmt(struct parser *p, uint32_t *id);

static bool
parse_array(struct parser *p, uint32_t *id)
{
	uint32_t s_id, c_id;
	struct node *n;

	if (!parse_stmt(p, &s_id)) {
		return false;
	}

	if (get_node(p->ast, s_id)->type == node_empty) {
		*id = s_id;
		return true;
	}

	if (!accept(p, tok_comma)) {
		n = make_node(p, id, node_argument);
		n->subtype = arg_normal;

		add_child(p, *id, node_child_l, s_id);
		return true;
	}

	if (!parse_array(p, &c_id)) {
		return false;
	}

	n = make_node(p, id, node_argument);
	n->subtype = arg_normal;

	add_child(p, *id, node_child_l, s_id);
	add_child(p, *id, node_child_c, c_id);

	return true;
}

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
			parse_error(p, "keyword argument key must be a plain identifier (not a %s)",
				node_type_to_s(get_node(p->ast, s_id)->type));
			return false;
		}

		if (!parse_stmt(p, &v_id)) {
			return false;
		}

		if (!accept(p, tok_comma)) {
			n = make_node(p, id, node_argument);
			n->subtype = at;

			add_child(p, *id, node_child_l, s_id);
			add_child(p, *id, node_child_r, v_id);
			return true;
		}
	} else if (!accept(p, tok_comma)) {
		n = make_node(p, id, node_argument);
		n->subtype = at;

		add_child(p, *id, node_child_l, s_id);
		return true;
	}

	if (!parse_args(p, &c_id)) {
		return false;
	}

	n = make_node(p, id, node_argument);
	n->subtype = at;

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

	if (!expect(p, tok_colon)) {
		return false;
	}

	if (!parse_stmt(p, &v_id)) {
		return false;
	}

	if (!accept(p, tok_comma)) {
		n = make_node(p, id, node_argument);
		n->subtype = arg_kwarg;

		add_child(p, *id, node_child_l, s_id);
		add_child(p, *id, node_child_r, v_id);
		return true;
	}

	if (!parse_key_values(p, &c_id)) {
		return false;
	}

	n = make_node(p, id, node_argument);
	n->subtype = arg_kwarg;

	add_child(p, *id, node_child_l, s_id);
	add_child(p, *id, node_child_r, v_id);
	add_child(p, *id, node_child_c, c_id);

	return true;
}

static bool
parse_index_call(struct parser *p, uint32_t *id, uint32_t l_id, bool have_l)
{
	uint32_t r_id;

	if (!parse_stmt(p, &r_id)) {
		return false;
	}

	if (get_node(p->ast, r_id)->type == node_empty) {
		parse_error(p, "empty index");
		return false;
	}

	if (!expect(p, tok_rbrack)) {
		return false;
	}

	make_node(p, id, node_index);
	if (have_l) {
		add_child(p, *id, node_child_l, l_id);
	}

	add_child(p, *id, node_child_r, r_id);

	return true;
}

static bool
parse_e9(struct parser *p, uint32_t *id)
{
	struct node *n;

	if (accept(p, tok_true)) {
		n = make_node(p, id, node_bool);
		n->subtype = 1;
	} else if (accept(p, tok_false)) {
		n = make_node(p, id, node_bool);
		n->subtype = 0;
	} else if (accept(p, tok_identifier)) {
		n = make_node(p, id, node_id);
	} else if (accept(p, tok_number)) {
		n = make_node(p, id, node_number);
	} else if (accept(p, tok_string)) {
		n = make_node(p, id, node_string);
	} else {
		make_node(p, id, node_empty);
	}

	return true;
}

static bool
parse_method_call(struct parser *p, uint32_t *id, uint32_t l_id, bool have_l)
{
	uint32_t meth_id, args, c_id = 0;
	bool have_c = false;
	struct token *start_tok = p->last;

	if (!parse_e9(p, &meth_id)) {
		return false;
	} else if (!expect(p, tok_lparen)) {
		return false;
	} else if (!parse_args(p, &args)) {
		return false;
	} else if (!expect(p, tok_rparen)) {
		return false;
	}

	struct node *n;
	n = make_node(p, id, node_method);
	n->subtype = have_c;

	if (have_l) {
		add_child(p, *id, node_child_l, l_id);
	}

	if (get_node(p->ast, meth_id)->type == node_empty) {
		p->last = start_tok;
		parse_error(p, "missing method name");
		return false;
	}

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
		if (!parse_stmt(p, id)) {
			return false;
		} else if (!expect(p, tok_rparen)) {
			return false;
		}

		return true;
	} else if (accept(p, tok_lbrack)) {
		make_node(p, id, node_array);

		if (!parse_array(p, &v)) {
			return false;
		}

		if (!expect(p, tok_rbrack)) {
			return false;
		}

		add_child(p, *id, node_child_l, v);
	} else if (accept(p, tok_lcurl)) {
		make_node(p, id, node_dict);

		if (!parse_key_values(p, &v)) {
			return false;
		}

		if (!expect(p, tok_rcurl)) {
			return false;
		}

		add_child(p, *id, node_child_l, v);
	} else {
		return parse_e9(p, id);
	}

	return true;
}

static bool
parse_chained(struct parser *p, uint32_t *id, uint32_t l_id, bool have_l)
{
	bool loop = false;
	if (accept(p, tok_dot)) {
		loop = true;

		if (have_l && get_node(p->ast, l_id)->type == node_empty) {
			parse_error(p, "cannot call a method on nothing");
			return false;
		}

		if (!parse_method_call(p, id, l_id, have_l)) {
			return false;
		}
	} else if (accept(p, tok_lbrack)) {
		loop = true;
		if (!parse_index_call(p, id, l_id, have_l)) {
			return false;
		}
	}

	if (loop) {
		uint32_t child_id;
		if (!parse_chained(p, &child_id, 0, false)) {
			return false;
		}

		if (child_id) {
			add_child(p, *id, node_child_d, child_id);
		}
		return true;
	} else {
		*id = l_id;
		return true;
	}
}

static bool
parse_e7(struct parser *p, uint32_t *id)
{
	uint32_t p_id, l_id;
	if (!(parse_e8(p, &l_id))) {
		return false;
	}

	if (accept(p, tok_lparen)) {
		uint32_t args, d_id;

		if (get_node(p->ast, l_id)->type != node_id) {
			parse_error(p, "Function call must be applied to plain id");
			return false;
		}

		if (!parse_args(p, &args)) {
			return false;
		} else if (!expect(p, tok_rparen)) {
			return false;
		}

		make_node(p, &p_id, node_function);
		add_child(p, p_id, node_child_l, l_id);
		add_child(p, p_id, node_child_r, args);

		if (!parse_chained(p, &d_id, 0, false)) {
			return false;
		}

		if (d_id) {
			add_child(p, p_id, node_child_d, d_id);
		}

		*id = p_id;

		return true;
	} else {
		return parse_chained(p, id, l_id, true);
	}
}

static bool
parse_e6(struct parser *p, uint32_t *id)
{
	uint32_t l_id;
	enum node_type t = 0;

	if (accept(p, tok_not)) {
		t = node_not;
	} else if (accept(p, tok_minus)) {
		t = node_u_minus;
	}

	struct token *op_tok = p->last_last;

	if (!(parse_e7(p, &l_id))) {
		return false;
	}

	if (t) {
		if (get_node(p->ast, l_id)->type == node_empty) {
			p->last = op_tok;
			parse_error(p, "missing operand to unary operator");
			return false;
		}
		make_node(p, id, t);
		add_child(p, *id, node_child_l, l_id);
	} else {
		*id = l_id;
	}

	return true;
}

static bool
parse_arith(struct parser *p, uint32_t *id, parse_func parse_upper,
	uint32_t map_start, uint32_t map_end)
{
	static const enum token_type map[] = {
		[arith_add] = tok_plus,
		[arith_sub] = tok_minus,
		[arith_mod] = tok_modulo,
		[arith_mul] = tok_star,
		[arith_div] = tok_slash,
	};
	struct node *n;

	uint32_t i, l_id, r_id;
	if (!(parse_upper(p, &l_id))) {
		return false;
	}

	struct token *op_tok = NULL;

	for (i = map_start; i < map_end; ++i) {
		if (accept(p, map[i])) {
			op_tok = p->last_last;

			if (!(parse_arith(p, &r_id, parse_upper, map_start, map_end))) {
				return false;
			}
			break;
		}
	}

	if (i != map_end) {
		switch (get_node(p->ast, l_id)->type) {
		case node_empty:
		case node_arithmetic:
			if (op_tok) {
				p->last = op_tok;
			}
			parse_error(p, "missing operand to binary operator");
			return false;
		default:
			break;
		}

		n = make_node(p, id, node_arithmetic);
		n->subtype = i;
		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, r_id);
	} else {
		*id = l_id;
	}

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
	n->subtype = comp;

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
	uint32_t l_id, r_id;
	if (!(parse_e4(p, &l_id))) {
		return false;
	}

	if (accept(p, tok_and)) {
		if (!parse_e3(p, &r_id)) {
			return false;
		}

		make_node(p, id, node_and);
		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, r_id);
	} else {
		*id = l_id;
	}

	return true;
}

static bool
parse_e2(struct parser *p, uint32_t *id)
{
	uint32_t l_id, r_id;
	if (!(parse_e3(p, &l_id))) {
		return false;
	}

	if (accept(p, tok_or)) {
		if (!parse_e2(p, &r_id)) {
			return false;
		}

		make_node(p, id, node_or);
		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, r_id);
	} else {
		*id = l_id;
	}

	return true;
}

static bool
parse_stmt(struct parser *p, uint32_t *id)
{
	uint32_t l_id = 0; // compiler thinks this won't get initialized...
	if (!(parse_e2(p, &l_id))) {
		return false;
	}

	if (accept(p, tok_plus_assign)) {
		uint32_t v, arith;
		make_node(p, &arith, node_arithmetic);

		if (get_node(p->ast, l_id)->type != node_id) {
			parse_error(p, "assignment target must be an id (got %s)", node_to_s(get_node(p->ast, l_id)));
			return false;
		} else if (!parse_stmt(p, &v)) {
			return false;
		}

		struct node *n = get_node(p->ast, arith);
		n->subtype = arith_add;
		add_child(p, arith, node_child_l, l_id);
		add_child(p, arith, node_child_r, v);

		make_node(p, id, node_assignment);
		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, arith);
	} else if (accept(p, tok_assign)) {
		uint32_t v;

		if (get_node(p->ast, l_id)->type != node_id) {
			parse_error(p, "assignment target must be an id (got %s)", node_to_s(get_node(p->ast, l_id)));
			return false;
		} else if (!parse_stmt(p, &v)) {
			return false;
		}

		/* NOTE: a bare ?: is actually valid in meson, none of the
		 * fields have to be filled. I'm making it an error here though,
		 * because missing fields in ternary expressions is probably an
		 * error
		 */
		if (get_node(p->ast, v)->type == node_empty) {
			parse_error(p, "missing rhs");
			return false;
		}

		make_node(p, id, node_assignment);
		add_child(p, *id, node_child_l, l_id);
		add_child(p, *id, node_child_r, v);
	} else if (accept(p, tok_question_mark)) {
		uint32_t a, b;

		if (!(parse_stmt(p, &a))) {
			return false;
		} else if (!expect(p, tok_colon)) {
			return false;
		} else if (!(parse_stmt(p, &b))) {
			return false;
		}

		if (get_node(p->ast, l_id)->type == node_empty) {
			parse_error(p, "missing condition expression");
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

static bool parse_block(struct parser *p, uint32_t *id);

static bool
parse_if(struct parser *p, uint32_t *id, enum if_type if_type)
{
	enum if_type child_type;
	uint32_t cond_id, block_id, c_id;
	bool have_c = false;

	if (if_type == if_normal) {
		if (!parse_stmt(p, &cond_id)) {
			return false;
		}
	}

	if (!expect(p, tok_eol)) {
		return false;
	}

	if (!parse_block(p, &block_id)) {
		return false;
	}

	if (if_type == if_normal) {
		if (accept(p, tok_elif)) {
			have_c = true;
			child_type = if_normal;
		} else if (accept(p, tok_else)) {
			have_c = true;
			child_type = if_else;
		}

		if (have_c) {
			if (!parse_if(p, &c_id, child_type)) {
				return false;
			}
		}
	}

	struct node *n = make_node(p, id, node_if);
	n->subtype = if_type;

	if (if_type == if_normal) {
		add_child(p, *id, node_child_l, cond_id);
	}

	add_child(p, *id, node_child_r, block_id);

	if (have_c) {
		add_child(p, *id, node_child_c, c_id);
	}

	return true;
}

static bool
parse_foreach_args(struct parser *p, uint32_t *id, uint32_t d)
{
	uint32_t l_id, r_id;
	bool have_r = false;

	if (!expect(p, tok_identifier)) {
		return false;
	}

	make_node(p, &l_id, node_id);

	if (d <= 0 && accept(p, tok_comma)) {
		have_r = true;
		if (!parse_foreach_args(p, &r_id, d + 1)) {
			return false;
		}
	}

	make_node(p, id, node_foreach_args);
	add_child(p, *id, node_child_l, l_id);

	if (have_r) {
		add_child(p, *id, node_child_r, r_id);
	}

	return true;
}

static bool
parse_foreach(struct parser *p, uint32_t *id)
{
	uint32_t args_id, r_id, c_id;
	if (!parse_foreach_args(p, &args_id, 0)) {
		return false;
	} else if (!expect(p, tok_colon)) {
		return false;
	} else if (!parse_stmt(p, &r_id)) {
		return false;
	} else if (!parse_block(p, &c_id)) {
		return false;
	}

	make_node(p, id, node_foreach);
	add_child(p, *id, node_child_l, args_id);
	add_child(p, *id, node_child_r, r_id);
	add_child(p, *id, node_child_c, c_id);

	return true;
}

static bool
parse_line(struct parser *p, uint32_t *id)
{
	if (p->last->type == tok_eol) {
		make_node(p, id, node_empty);
	} else if (accept(p, tok_if)) {
		if (!parse_if(p, id, if_normal)) {
			return false;
		}
		return expect(p, tok_endif);
	} else if (accept(p, tok_foreach)) {
		if (!parse_foreach(p, id)) {
			return false;
		}
		return expect(p, tok_endforeach);
	} else if (accept(p, tok_continue)) {
		make_node(p, id, node_continue);
	} else if (accept(p, tok_break)) {
		make_node(p, id, node_break);
	} else {
		return parse_stmt(p, id);
	}

	return true;
}

static bool
parse_block(struct parser *p, uint32_t *id)
{
	uint32_t l_id, r_id;
	bool loop = true, have_eol = true, have_r = false;

	while (loop) {
		if (!parse_line(p, &l_id)) {
			return false;
		}

		if (get_node(p->ast, l_id)->type != node_empty) {
			loop = false;
		}

		if (!accept(p, tok_eol)) {
			have_eol = false;
			break;
		}
	}

	if (have_eol) {
		if (!parse_block(p, &r_id)) {
			return false;
		}
		have_r = true;
	}

	if (get_node(p->ast, l_id)->type == node_empty) {
		assert(!have_r);
		*id = l_id;
		return true;
	}

	make_node(p, id, node_block);
	add_child(p, *id, node_child_l, l_id);
	if (have_r) {
		add_child(p, *id, node_child_r, r_id);
	}

	return true;
}

bool
parser_parse(struct ast *ast, struct source_data *sdata, struct source *src)
{
	bool ret = false;
	struct tokens toks;

	if (!lexer_lex(&toks, sdata, src)) {
		goto ret;
	}

	struct parser parser = { .src = src, .ast = ast, .toks = &toks };

	darr_init(&ast->nodes, 2048, sizeof(struct node));
	uint32_t id;
	make_node(&parser, &id, node_null);
	assert(id == 0);

	get_next_tok(&parser);

	if (!parse_block(&parser, &ast->root)) {
		goto ret;
	} else if (!expect(&parser, tok_eof)) {
		goto ret;
	}

	ret = true;
ret:
	tokens_destroy(&toks);
	return ret;
}

static void
print_tree(struct ast *ast, uint32_t id, uint32_t d, char label)
{
	struct node *n = get_node(ast, id);
	uint32_t i;

	for (i = 0; i < d; ++i) {
		putc(' ', stdout);
	}

	printf("%c:%s\n", label, node_to_s(n));

	static const char *child_labels = "lrcd";

	for (i = 0; i < NODE_MAX_CHILDREN; ++i) {
		if ((1 << i) & n->chflg) {
			print_tree(ast, get_child(ast, id, i), d + 1, child_labels[i]);
		}
	}
}

void
print_ast(struct ast *ast)
{
	print_tree(ast, ast->root, 0, 'l');
}

void
ast_destroy(struct ast *ast)
{
	darr_destroy(&ast->nodes);
}
