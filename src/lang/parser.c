/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

#include "buf_size.h"
#include "error.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/typecheck.h"
#include "lang/vm.h"
#include "lang/workspace.h"
#include "platform/assert.h"
#include "tracy.h"

/******************************************************************************
 * parser
 ******************************************************************************/

struct parser;

enum parse_stmt_flags {
	parse_stmt_flag_eol_optional = 1 << 0,
};

struct parse_behavior {
	void (*advance)(struct parser *p);
	struct node *(*parse_stmt)(struct parser *p, enum parse_stmt_flags flags);
	struct node *(*parse_list)(struct parser *p, enum node_type t, enum token_type end);
};

enum cm_parse_mode {
	cm_parse_mode_command,
	cm_parse_mode_command_args,
	cm_parse_mode_conditional,
};

struct parser {
	struct token previous, current, next;
	struct lexer lexer;
	const struct parse_rule *parse_rules;
	struct workspace *wk;
	const struct source *src;
	struct bucket_arr *nodes;
	enum vm_compile_mode mode;
	enum cm_parse_mode cm_mode;
	uint32_t inside_loop;
	obj doc_comment;

	struct {
		char msg[2048];
		uint32_t len;
		uint32_t count;
		bool unwinding;
	} err;

	struct {
		struct node_fmt previous, current;
	} fmt;

	struct parse_behavior behavior;
};

enum parse_precedence {
	parse_precedence_none,
	parse_precedence_assignment, // =
	parse_precedence_or, // OR
	parse_precedence_and, // AND
	parse_precedence_equality, // == !=
	parse_precedence_comparison, // < > <= >=
	parse_precedence_term, // + -
	parse_precedence_factor, // * /
	parse_precedence_unary, // ! -
	parse_precedence_call, // . ()
	parse_precedence_primary,
};

typedef struct node *((*parse_prefix_fn)(struct parser *p, bool assignment_allowed));
typedef struct node *((*parse_infix_fn)(struct parser *p, struct node *l, bool assignment_allowed));

struct parse_rule {
	parse_prefix_fn prefix;
	parse_infix_fn infix;
	enum parse_precedence precedence;
};

static struct node *parse_prec(struct parser *p, enum parse_precedence prec);
static struct node *parse_expr(struct parser *p);
static struct node *parse_block(struct parser *p, enum token_type types[], uint32_t types_len, enum parse_stmt_flags);

/*******************************************************************************
 * misc api functions
 ******************************************************************************/

const char *
node_type_to_s(enum node_type t)
{
#define nt(__n) \
	case node_type_##__n: return #__n;

	switch (t) {
		nt(bool);
		nt(null);
		nt(id);
		nt(id_lit);
		nt(number);
		nt(string);
		nt(continue);
		nt(break);
		nt(args);
		nt(def_args);
		nt(list);
		nt(dict);
		nt(array);
		nt(kw);
		nt(or);
		nt(and);
		nt(in);
		nt(not_in);
		nt(eq);
		nt(neq);
		nt(lt);
		nt(leq);
		nt(gt);
		nt(geq);
		nt(add);
		nt(sub);
		nt(div);
		nt(mul);
		nt(mod);
		nt(not );
		nt(index);
		nt(member);
		nt(call);
		nt(assign);
		nt(foreach_args);
		nt(foreach);
		nt(if);
		nt(negate);
		nt(ternary);
		nt(stmt);
		nt(stringify);
		nt(func_def);
		nt(return);
		nt(group);
		nt(maybe_id);
	}

	UNREACHABLE_RETURN;

#undef nt
}

const char *
fmt_node_to_s(struct workspace *wk, const struct node *n)
{
	static char buf[BUF_SIZE_S + 1];
	uint32_t i = 0;

	i += snprintf(&buf[i], BUF_SIZE_S - i, "%s", node_type_to_s(n->type));

	switch (n->type) {
	case node_type_maybe_id:
	case node_type_id:
	case node_type_id_lit:
	case node_type_number:
	case node_type_string: i += obj_snprintf(wk, &buf[i], BUF_SIZE_S - i, ":%o", n->data.str); break;
	case node_type_bool: break;
	default: break;
	}

	i += obj_snprintf(wk, &buf[i], BUF_SIZE_S - i, ":%o:%o", n->fmt.pre.ws, n->fmt.post.ws);

	return buf;
}

const char *
node_to_s(struct workspace *wk, const struct node *n)
{
	static char buf[BUF_SIZE_S + 1];
	uint32_t i = 0;

	i += snprintf(&buf[i], BUF_SIZE_S - i, "%s[%d,%d]", node_type_to_s(n->type), n->location.off, n->location.len);

	switch (n->type) {
	case node_type_maybe_id:
	case node_type_id:
	case node_type_id_lit:
	case node_type_string: i += obj_snprintf(wk, &buf[i], BUF_SIZE_S - i, ":%o", n->data.str); break;
	case node_type_number:
	case node_type_bool: i += snprintf(&buf[i], BUF_SIZE_S - i, ":%" PRId64, n->data.num); break;
	default: break;
	}

	return buf;
}

static void
print_fmt_ast_at(struct workspace *wk, struct node *n, uint32_t d, char label)
{
	uint32_t i;

	for (i = 0; i < d; ++i) {
		log_raw("  ");
	}

	log_raw("%c:%s\n", label, fmt_node_to_s(wk, n));

	if (n->l) {
		print_fmt_ast_at(wk, n->l, d + 1, 'l');
	}
	if (n->r) {
		print_fmt_ast_at(wk, n->r, d + 1, 'r');
	}
}

static void
print_ast_at(struct workspace *wk, struct node *n, uint32_t d, char label)
{
	uint32_t i;

	for (i = 0; i < d; ++i) {
		log_raw("  ");
	}

	log_raw("%c:%s\n", label, node_to_s(wk, n));

	if (n->l) {
		print_ast_at(wk, n->l, d + 1, 'l');
	}
	if (n->r) {
		print_ast_at(wk, n->r, d + 1, 'r');
	}
}

void
print_fmt_ast(struct workspace *wk, struct node *root)
{
	print_fmt_ast_at(wk, root, 0, 'l');
}

void
print_ast(struct workspace *wk, struct node *root)
{
	print_ast_at(wk, root, 0, 'l');
}

static struct source_location
source_location_merge(struct source_location a, struct source_location b)
{
	uint32_t off = a.off < b.off ? a.off : b.off;
	uint32_t a_end = a.off + a.len;
	uint32_t b_end = b.off + b.len;
	uint32_t len = a_end > b_end ? a_end - off : b_end - off;

	return (struct source_location){ .off = off, .len = len };
}
/******************************************************************************
 * error handling
 ******************************************************************************/

static void
parse_diagnostic(struct parser *p, struct source_location *l, enum log_level lvl)
{
	if (p->err.unwinding) {
		return;
	}

	if (!l) {
		l = &p->previous.location;
	}

	if (!(p->mode & vm_compile_mode_quiet)) {
		error_message(p->src, *l, lvl, 0, p->err.msg);
	}

	if (lvl == log_error) {
		++p->err.count;
		p->err.unwinding = true;
	}
}

MUON_ATTR_FORMAT(printf, 3, 4)
static void
parse_error(struct parser *p, struct source_location *l, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(p->err.msg, ARRAY_LEN(p->err.msg), fmt, args);
	va_end(args);

	parse_diagnostic(p, l, log_error);
}

static void
parse_advance(struct parser *p)
{
	p->previous = p->current;
	p->fmt.previous = p->fmt.current;
	lexer_next(&p->lexer, &p->current);

	if (p->current.type == token_type_doc_comment) {
		p->doc_comment = p->current.data.str;
		lexer_next(&p->lexer, &p->current);
	}

	while (p->current.type == token_type_error) {
		parse_error(p, &p->current.location, "%s", get_cstr(p->wk, p->current.data.str));
		p->err.unwinding = false;
		lexer_next(&p->lexer, &p->current);
	}

	if (p->mode & vm_compile_mode_fmt) {
		p->fmt.current.ws = lexer_get_preceeding_whitespace(&p->lexer);

		// In fmt mode, merge all consecutive eol tokens into one and
		// store the information in the ws field.
		if (p->current.type == token_type_eol) {
			struct lexer lexer_peek = p->lexer, new_lexer;
			struct token next, new_current = { 0 };
			while (true) {
				lexer_next(&lexer_peek, &next);
				if (next.type != token_type_eol) {
					break;
				}
				new_current = next;
				new_lexer = lexer_peek;
			}

			if (new_current.type == token_type_eol) {
				str_appn(p->wk,
					&p->fmt.current.ws,
					&p->lexer.src[p->current.location.off],
					new_current.location.off - p->current.location.off + 1);

				p->current = new_current;
				p->lexer = new_lexer;
			}
		}
	}

	if (p->current.type == token_type_not) {
		struct lexer lexer_peek = p->lexer;
		struct token next;
		lexer_next(&lexer_peek, &next);
		if (next.type == token_type_in) {
			p->current.type = token_type_not_in;
			p->lexer = lexer_peek;
		}
	} else if (p->current.type == token_type_eof) {
		if (p->previous.type != token_type_eol) {
			// simulate a eol at the end of the file even if we
			// didn't get one
			p->current.location.len = 0;
			p->current.type = token_type_eol;
		}
	}

	/* LL("previous: %s, current: ", token_to_s(p->wk, &p->previous)); */
	/* log_plain("%s\n", token_to_s(p->wk, &p->current)); */

	/* obj_fprintf(p->wk, log_file(), "%o, %o\n", p->fmt.previous.ws, p->fmt.current.ws); */

	/* list_line_range(p->src, p->current.location, 0); */
}

static bool
parse_accept(struct parser *p, enum token_type type)
{
	if (p->current.type == type) {
		p->behavior.advance(p);
		return true;
	}

	return false;
}

static bool
parse_match(struct parser *p, enum token_type types[], uint32_t types_len)
{
	if (!types_len) {
		return false;
	} else if (types_len == 1) {
		return parse_accept(p, types[0]);
	}

	if (p->current.type != types[0]) {
		return false;
	}

	struct lexer lexer_peek = p->lexer;
	struct token next;

	uint32_t i;
	for (i = 1; i < types_len; ++i) {
		lexer_next(&lexer_peek, &next);
		if (next.type != types[i]) {
			return false;
		}
	}

	return true;
}

static bool
parse_expect_noadvance(struct parser *p, enum token_type type)
{
	if (p->current.type != type) {
		parse_error(p,
			&p->current.location,
			"expected %s not %s",
			token_type_to_s(type),
			token_type_to_s(p->current.type));
		return false;
	}

	return true;
}

static bool
parse_expect(struct parser *p, enum token_type type)
{
	if (!parse_expect_noadvance(p, type)) {
		return false;
	}
	p->behavior.advance(p);
	return true;
}

static struct node *
make_node(struct parser *p, struct node *n)
{
	n = bucket_arr_push(p->nodes, n);
	if (p->previous.type) {
		n->location = p->previous.location;
		n->data = p->previous.data;
		n->fmt.pre = p->fmt.previous;
	}
	return n;
}

static struct node *
make_node_t(struct parser *p, enum node_type t)
{
	return make_node(p, &(struct node){ .type = t });
}

static struct node *
make_node_assign(struct parser *p, enum op_store_flags flags)
{
	struct node *n = make_node_t(p, node_type_assign);
	n->data.type = flags;

	switch (p->previous.type) {
	case '=': {
		break;
	}
	case token_type_plus_assign: {
		n->data.type |= op_store_flag_add_store;
		break;
	}
	default: UNREACHABLE;
	}

	return n;
}

/******************************************************************************
 * parsing functions
 ******************************************************************************/

static struct node *
parse_number(struct parser *p, bool assignment_allowed)
{
	return make_node_t(p, node_type_number);
}

static bool
id_is_assignable(const struct str *id)
{
	return !(str_eql(id, &STR("meson")) || str_eql(id, &STR("build_machine")) || str_eql(id, &STR("host_machine"))
		 || str_eql(id, &STR("target_machine")));
}

static struct node *
parse_id(struct parser *p, bool assignment_allowed)
{
	struct node *id = make_node_t(p, node_type_id);

	if (assignment_allowed && (parse_accept(p, '=') || parse_accept(p, token_type_plus_assign))) {
		if (!id_is_assignable(get_str(p->wk, id->data.str))) {
			parse_error(p, &id->location, "'%s' is not assignable", get_str(p->wk, id->data.str)->s);
			return id;
		}

		struct node *n = make_node_assign(p, 0);
		n->location = id->location;
		id->type = node_type_id_lit;
		n->l = id;
		n->r = parse_expr(p);
		return n;
	} else {
		return id;
	}
}

static struct node *
parse_string(struct parser *p, bool assignment_allowed)
{
	return make_node_t(p, node_type_string);
}

static struct node *
parse_bool(struct parser *p, bool assignment_allowed)
{
	struct node *n = make_node_t(p, node_type_bool);
	n->data.num = p->previous.type == token_type_true;
	return n;
}

static struct node *
parse_null(struct parser *p, bool assignment_allowed)
{
	return make_node_t(p, node_type_null);
}

static struct node *
parse_fstring(struct parser *p, bool assignment_allowed)
{
	uint32_t i, j;
	const struct str *fstr = get_str(p->wk, p->previous.data.str);
	struct str str = { fstr->s }, identifier;

	struct node *n, *res, *lhs = 0, *rhs = 0, *prev_rhs;

	res = n = 0;

	for (i = 0; i < fstr->len;) {
		if (fstr->s[i] == '@' && is_valid_start_of_identifier(fstr->s[i + 1])) {
			for (j = i + 1; j < fstr->len && is_valid_inside_of_identifier(fstr->s[j]); ++j) {
			}

			if (fstr->s[j] == '@' && (identifier.len = j - i - 1) > 0) {
				identifier.s = &fstr->s[i + 1];

				if (str.len) {
					lhs = make_node_t(p, node_type_string);
					lhs->data.str = make_strn(p->wk, str.s, str.len);
				} else {
					lhs = 0;
				}

				rhs = make_node_t(p, node_type_stringify);
				rhs->l = make_node_t(p, node_type_id);
				rhs->l->data.str = make_strn(p->wk, identifier.s, identifier.len);

				if (lhs) {
					if (!n) {
						res = n = make_node_t(p, node_type_add);
					} else {
						if (n->type == node_type_add) {
							prev_rhs = n->r;
							n = n->r = make_node_t(p, node_type_add);
						} else {
							prev_rhs = n;
							res = n = make_node_t(p, node_type_add);
						}

						n->l = prev_rhs;
						n = n->r = make_node_t(p, node_type_add);
					}

					n->l = lhs;
					n->r = rhs;
				} else {
					if (!n) {
						res = n = rhs;
					} else {
						if (n->type == node_type_add) {
							prev_rhs = n->r;
							n = n->r = make_node_t(p, node_type_add);
						} else {
							prev_rhs = n;
							res = n = make_node_t(p, node_type_add);
						}

						n->l = prev_rhs;
						n->r = rhs;
					}
				}

				i = j + 1;

				str = (struct str){ &fstr->s[i] };
				continue;
			}
		}

		++str.len;
		++i;
	}

	if (str.len) {
		if (!n) {
			res = n = make_node_t(p, node_type_string);
		} else {
			if (n->type == node_type_add) {
				prev_rhs = n->r;
				n = n->r = make_node_t(p, node_type_add);
			} else {
				prev_rhs = n;
				res = n = make_node_t(p, node_type_add);
			}

			n->l = prev_rhs;
			n = n->r = make_node_t(p, node_type_string);
		}
		n->data.str = make_strn(p->wk, str.s, str.len);
	}

	return res;
}

static struct node *
parse_binary(struct parser *p, struct node *l, bool assignment_allowed)
{
	enum node_type t;
	enum token_type prev = p->previous.type;
	struct node *n, *r;

	r = parse_prec(p, p->parse_rules[prev].precedence + 1);

	switch (prev) {
	case '+': t = node_type_add; break;
	case '-': t = node_type_sub; break;
	case '/': t = node_type_div; break;
	case '*': t = node_type_mul; break;
	case '%': t = node_type_mod; break;
	case '<': t = node_type_lt; break;
	case '>': t = node_type_gt; break;
	case token_type_eq: t = node_type_eq; break;
	case token_type_neq: t = node_type_neq; break;
	case token_type_leq: t = node_type_leq; break;
	case token_type_geq: t = node_type_geq; break;
	case token_type_or: t = node_type_or; break;
	case token_type_and: t = node_type_and; break;
	case token_type_in: t = node_type_in; break;
	case token_type_not_in: t = node_type_not_in; break;
	default: UNREACHABLE;
	}

	n = make_node_t(p, t);
	n->l = l;
	n->r = r;
	return n;
}

static struct node *
parse_ternary(struct parser *p, struct node *l, bool assignment_allowed)
{
	struct node *n;

	n = make_node_t(p, node_type_ternary);
	n->l = l;
	n->r = make_node_t(p, node_type_list);
	n->r->l = parse_prec(p, parse_precedence_assignment);
	parse_expect(p, ':');
	n->r->r = parse_prec(p, parse_precedence_assignment);
	return n;
}

static struct node *
parse_index(struct parser *p, struct node *l, bool assignment_allowed)
{
	struct node *n, *key;

	key = parse_prec(p, parse_precedence_assignment);
	parse_expect(p, ']');

	if ((p->mode & vm_compile_mode_language_extended) && assignment_allowed
		&& (parse_accept(p, '=') || parse_accept(p, token_type_plus_assign))) {
		n = make_node_assign(p, op_store_flag_member);
		n->location = key->location;
		n->l = key;
		n->r = make_node_t(p, node_type_list);
		n->r->l = l;
		n->r->r = parse_expr(p);
	} else {
		n = make_node_t(p, node_type_index);
		n->l = l;
		n->r = key;
	}

	return n;
}

static struct node *
parse_unary(struct parser *p, bool assignment_allowed)
{
	struct node *n;
	switch (p->previous.type) {
	case '-': n = make_node_t(p, node_type_negate); break;
	case token_type_not: n = make_node_t(p, node_type_not); break;
	default: UNREACHABLE;
	}

	n->l = parse_prec(p, parse_precedence_unary);
	return n;
}

static struct node *
parse_grouping(struct parser *p, bool assignment_allowed)
{
	struct node *n = parse_prec(p, parse_precedence_assignment);
	parse_expect(p, ')');
	return n;
}

static struct node *
parse_grouping_fmt(struct parser *p, bool assignment_allowed)
{
	struct node *n = make_node_t(p, node_type_group);
	n->l = parse_prec(p, parse_precedence_assignment);
	parse_expect(p, ')');
	n->l->fmt.post = p->fmt.previous;
	/* n->fmt.post = p->fmt.current; */
	return n;
}

static bool
parse_type(struct parser *p, type_tag *type, bool top_level)
{
	*type = 0;

	const char *typestr = 0;
	if (parse_accept(p, token_type_identifier)) {
		typestr = get_cstr(p->wk, p->previous.data.str);
		type_tag t;
		if (s_to_type_tag(typestr, &t)) {
			*type = t;
		} else {
			parse_error(p, NULL, "unknown type %s", typestr);
			return false;
		}
	} else if (parse_accept(p, token_type_null)) {
		typestr = "null";
		*type = TYPE_TAG_ALLOW_NULL;
	} else {
		return true;
	}

	if (!top_level) {
		const char *err_type = 0;
		if ((*type & TYPE_TAG_LISTIFY)) {
			err_type = "listify";
		} else if ((*type & TYPE_TAG_GLOB)) {
			err_type = "glob";
		}

		if (err_type) {
			parse_error(
				p, &p->previous.location, "%s can only be specified as the top level type", err_type);
			return false;
		}
	}

	bool has_sub_type = *type == TYPE_TAG_LISTIFY || *type == TYPE_TAG_GLOB || *type == tc_dict
			    || *type == tc_array;

	if (has_sub_type) {
		if (!parse_accept(p, token_type_lbrack)) {
			parse_error(p,
				&p->previous.location,
				"the type %s requires a sub type (e.g. %s[any])",
				typestr,
				typestr);
			return false;
		}

		type_tag sub_type;
		if (!parse_type(p, &sub_type, false)) {
			return false;
		}

		if (!sub_type) {
			parse_error(p, &p->previous.location, "expected type");
		}

		if (!parse_expect(p, token_type_rbrack)) {
			return false;
		}

		if (*type == TYPE_TAG_LISTIFY || *type == TYPE_TAG_GLOB) {
			*type |= sub_type;
		} else if (*type == tc_dict || *type == tc_array) {
			*type = make_complex_type(p->wk, complex_type_nested, *type, sub_type);
		} else {
			UNREACHABLE;
		}
	}

	if (parse_accept(p, token_type_bitor)) {
		type_tag ord_type;
		if (!parse_type(p, &ord_type, false)) {
			return false;
		}

		if ((ord_type & TYPE_TAG_COMPLEX) || (*type & TYPE_TAG_COMPLEX)) {
			*type = make_complex_type(p->wk, complex_type_or, *type, ord_type);
		} else {
			*type |= ord_type;
		}
	}

	return true;
}

static struct node *
parser_get_doc_comment(struct parser *p)
{
	struct node *n = 0;
	if (p->doc_comment) {
		n = make_node_t(p, node_type_string);
		n->data.str = p->doc_comment;
		p->doc_comment = 0;
	}
	return n;
}

static struct node *
parse_list(struct parser *p, enum node_type t, enum token_type end)
{
	const bool relaxed = p->mode & vm_compile_mode_relaxed_parse;
	bool got_kw = false;
	uint32_t len = 0, kwlen = 0;
	struct node *n, *res, *val, *key;
	type_tag type = 0;

	res = n = make_node_t(p, t);
	while (p->current.type != end && p->current.type != token_type_eof) {
		key = 0;

		switch (t) {
		case node_type_array: val = parse_expr(p); break;
		case node_type_dict:
			key = parse_expr(p);
			parse_expect(p, ':');
			val = parse_expr(p);
			break;
		case node_type_args:
			if (parse_match(p, (enum token_type[]){ token_type_identifier, ':' }, 2)) {
				parse_expect(p, token_type_identifier);
				key = parse_id(p, false);
				key->type = node_type_string;
				parse_expect(p, ':');
			}
			val = parse_expr(p);
			break;
		case node_type_def_args: {
			struct node *doc = parser_get_doc_comment(p);
			parse_expect(p, token_type_identifier);
			val = parse_id(p, false);
			parse_type(p, &type, true);

			if (!type) {
				parse_error(p, 0, "expected type");
			}

			val->l = make_node_t(p, node_type_list);
			val->l->data.type = type;
			val->r = doc;

			if (parse_accept(p, ':')) {
				key = val;
				key->type = node_type_string;
				if (!(p->current.type == ',' || p->current.type == end)) {
					val = parse_expr(p);
				} else {
					val = 0;
				}
			}
			break;
		}
		default: UNREACHABLE; break;
		}

		if (got_kw && !key) {
			parse_error(p, 0, "non kwarg not allowed after kwarg");
		}

		if (!val && p->mode & vm_compile_mode_fmt) {
			val = make_node_t(p, node_type_list);
		}

		if (key) {
			got_kw = true;
			n->l = make_node_t(p, node_type_kw);
			n->l->r = key;
			n->l->l = val;
			++kwlen;
		} else {
			n->l = val;
			++len;
		}

		if (!parse_accept(p, ',')) {
			if (n->l) {
				n->l->fmt.post = p->fmt.current;
			}

			if (!relaxed) {
				break;
			}
		}

		if (p->current.type == end && n->l) {
			// Don't break here, let n->r be made and then break.
			// This is so the formatter can know there was a
			// trailing comma here.
			n->l->fmt.post = p->fmt.current;
		}

		n = n->r = make_node_t(p, node_type_list);
	}

	parse_expect(p, end);

	res->location = source_location_merge(res->location, p->previous.location);

	res->data.len.args = len;
	res->data.len.kwargs = kwlen;
	return res;
}

static struct node *
parse_array(struct parser *p, bool assignment_allowed)
{
	return p->behavior.parse_list(p, node_type_array, ']');
}

static struct node *
parse_dict(struct parser *p, bool assignment_allowed)
{
	return p->behavior.parse_list(p, node_type_dict, '}');
}

static struct node *
parse_call(struct parser *p, struct node *l, bool assignment_allowed)
{
	struct node *n;
	n = make_node_t(p, node_type_call);
	n->r = l;
	n->l = p->behavior.parse_list(p, node_type_args, ')');

	n->location = source_location_merge(l->location, p->previous.location);

	if (n->r->type == node_type_id) {
		n->r->type = node_type_id_lit;
	}
	return n;
}

static struct node *
parse_member(struct parser *p, struct node *l, bool assignment_allowed)
{
	struct node *n, *id;

	const bool relaxed = p->mode & vm_compile_mode_relaxed_parse;

	if ((relaxed ? parse_accept : parse_expect)(p, token_type_identifier)) {
		id = parse_id(p, false);
		id->type = node_type_id_lit;
	} else {
		id = make_node_t(p, node_type_id_lit);
		id->data.str = make_str(p->wk, "");
	}

	if ((p->mode & vm_compile_mode_language_extended) && assignment_allowed
		&& (parse_accept(p, '=') || parse_accept(p, token_type_plus_assign))) {
		n = make_node_assign(p, op_store_flag_member);
		n->location = id->location;
		if (!(p->mode & vm_compile_mode_fmt)) {
			id->type = node_type_string;
		}
		n->l = id;
		n->r = make_node_t(p, node_type_list);
		n->r->l = l;
		n->r->r = parse_expr(p);
	} else {
		n = make_node_t(p, node_type_member);
		n->l = l;
		n->r = id;

		if (!((p->mode & vm_compile_mode_language_extended) || relaxed)) {
			parse_expect_noadvance(p, '(');
		}
	}

	return n;
}

static struct node *
parse_func_params_and_body(struct parser *p, struct node *id)
{
	struct node *n = make_node_t(p, node_type_func_def);

	n->l = make_node_t(p, node_type_list);
	n->l->l = make_node_t(p, node_type_list);
	n->l->l->l = id;
	n->l->l->r = parser_get_doc_comment(p);

	parse_expect(p, '(');
	n->l->r = p->behavior.parse_list(p, node_type_def_args, ')');

	if (parse_accept(p, token_type_returntype)) {
		parse_type(p, &n->data.type, true);
		if (!n->data.type) {
			parse_error(p, 0, "expected type");
		}
	} else {
		n->data.type = 0;
	}

	parse_accept(p, token_type_eol);

	n->r = parse_block(p, (enum token_type[]){ token_type_endfunc }, 1, parse_stmt_flag_eol_optional);
	parse_expect(p, token_type_endfunc);

	return n;
}

static struct node *
parse_func(struct parser *p, bool assignment_allowed)
{
	return parse_func_params_and_body(p, 0);
}

static struct node *
parse_expr(struct parser *p)
{
	return parse_prec(p, parse_precedence_assignment);
}

static struct node *
parse_prec(struct parser *p, enum parse_precedence prec)
{
	p->behavior.advance(p);

	if (!p->parse_rules[p->previous.type].prefix) {
		parse_error(p, 0, "expected expression, got %s", token_type_to_s(p->previous.type));
		return 0;
	}

	bool assignment_allowed = prec <= parse_precedence_assignment;
	struct node *l = p->parse_rules[p->previous.type].prefix(p, assignment_allowed);

	while (prec <= p->parse_rules[p->current.type].precedence) {
		p->behavior.advance(p);
		l = p->parse_rules[p->previous.type].infix(p, l, assignment_allowed);
	}

	return l;
}

static struct node *
parse_stmt(struct parser *p, enum parse_stmt_flags flags)
{
	struct node *n;

	if (parse_accept(p, token_type_if)) {
		struct node *parent;
		parent = n = make_node_t(p, node_type_if);
		while (true) {
			n->l = make_node_t(p, node_type_list);
			n->l->l = p->previous.type == token_type_else ? 0 : parse_expr(p);
			parse_expect(p, token_type_eol);
			n->l->r = parse_block(
				p, (enum token_type[]){ token_type_elif, token_type_else, token_type_endif }, 3, 0);

			if (!(parse_accept(p, token_type_elif) || parse_accept(p, token_type_else))) {
				break;
			}

			n = n->r = make_node_t(p, node_type_if);
		}

		parse_expect(p, token_type_endif);
		n = parent;
	} else if (parse_accept(p, token_type_foreach)) {
		n = make_node_t(p, node_type_foreach);
		n->l = make_node_t(p, node_type_foreach_args);

		parse_expect(p, token_type_identifier);
		n->l->l = make_node_t(p, node_type_list);
		n->l->l->l = parse_id(p, false);

		if (parse_accept(p, ',')) {
			parse_expect(p, token_type_identifier);
			n->l->l->r = parse_id(p, false);
		}

		parse_expect(p, ':');
		n->l->r = parse_expr(p);

		parse_expect(p, token_type_eol);

		++p->inside_loop;
		n->r = parse_block(p, (enum token_type[]){ token_type_endforeach }, 1, 0);
		--p->inside_loop;

		parse_expect(p, token_type_endforeach);
	} else if (p->inside_loop && parse_accept(p, token_type_continue)) {
		n = make_node_t(p, node_type_continue);
	} else if (p->inside_loop && parse_accept(p, token_type_break)) {
		n = make_node_t(p, node_type_break);
	} else if (parse_accept(p, token_type_func)) {
		parse_expect(p, token_type_identifier);
		n = parse_func_params_and_body(p, parse_id(p, false));
	} else if (parse_accept(p, token_type_return)) {
		n = make_node_t(p, node_type_return);

		if (p->current.type != token_type_eol) {
			n->l = parse_expr(p);
		}
	} else {
		n = parse_expr(p);
	}

	if (p->err.unwinding) {
		while (p->err.unwinding && p->current.type != token_type_eof) {
			switch (p->current.type) {
			case token_type_eol: p->err.unwinding = false; break;
			default: break;
			}

			if (p->err.unwinding) {
				p->behavior.advance(p);
			}
		}
	} else {
		if (flags & parse_stmt_flag_eol_optional) {
			parse_accept(p, token_type_eol);
		} else {
			parse_expect(p, token_type_eol);
		}
	}

	return n;
}

static bool
parse_block_skip_eol_and_check_terminator(struct parser *p, enum token_type types[], uint32_t types_len)
{
	while (parse_accept(p, token_type_eol)) {
	}

	uint32_t i;
	for (i = 0; i < types_len; ++i) {
		if (p->current.type == types[i]) {
			return true;
		}
	}

	return parse_accept(p, token_type_eof);
}

static struct node *
parse_block(struct parser *p, enum token_type types[], uint32_t types_len, enum parse_stmt_flags flags)
{
	struct node *res, *n;

	res = n = 0;

	while (true) {
		if (parse_block_skip_eol_and_check_terminator(p, types, types_len)) {
			break;
		}

		if (!n) {
			res = n = make_node_t(p, node_type_stmt);
		}

		n->l = p->behavior.parse_stmt(p, flags);

		if ((flags & parse_stmt_flag_eol_optional) && p->previous.type != token_type_eol) {
			break;
		} else if (parse_block_skip_eol_and_check_terminator(p, types, types_len)) {
			break;
		}

		n = n->r = make_node_t(p, node_type_stmt);
	}

	if (p->mode & vm_compile_mode_fmt) {
		if (n) {
			n->fmt.post = p->fmt.previous;
		} else {
			res = n = make_node_t(p, node_type_stmt);
		}
	}

	return res;
}

static struct node *
parse_impl(struct workspace *wk,
	const struct source *src,
	enum vm_compile_mode mode,
	const struct parse_behavior *behavior,
	const struct parse_rule *rules,
	struct parser *p)
{
	TracyCZoneAutoS;

	*p = (struct parser){
		.wk = wk,
		.nodes = &wk->vm.compiler_state.nodes,
		.src = src,
		.mode = mode,
		.parse_rules = rules,
		.behavior = *behavior,
	};

	enum lexer_mode lexer_mode = 0;
	if (p->mode & vm_compile_mode_language_extended) {
		lexer_mode |= lexer_mode_functions;
	}
	if (p->mode & vm_compile_mode_fmt) {
		lexer_mode |= lexer_mode_fmt;
	}
	lexer_init(&p->lexer, p->wk, p->src, lexer_mode);

	p->behavior.advance(p);

	struct node *n = parse_block(p, (enum token_type[]){ token_type_eof }, 1, 0);

	if (!p->err.count && !n) {
		// This is an empty file.  Add a single dummy statement.
		n = make_node_t(p, node_type_stmt);
	}

	lexer_destroy(&p->lexer);

	TracyCZoneAutoE;
	return p->err.count ? 0 : n;
}

// clang-format off
static const struct parse_rule parse_rules_base[token_type_count] = {
	[token_type_number]      = { parse_number,   0,             0                           },
	[token_type_identifier]  = { parse_id,       0,             0                           },
	[token_type_string]      = { parse_string,   0,             0                           },
	[token_type_fstring]     = { parse_fstring,  0,             0                           },
	[token_type_true]        = { parse_bool,     0,             0                           },
	[token_type_false]       = { parse_bool,     0,             0                           },
	[token_type_null]        = { parse_null,     0,             0                           },
	['(']                    = { parse_grouping, parse_call,    parse_precedence_call       },
	['[']                    = { parse_array,    parse_index,   parse_precedence_call       },
	['{']                    = { parse_dict,     0,             0                           },
	['+']                    = { 0,              parse_binary,  parse_precedence_term       },
	['-']                    = { parse_unary,    parse_binary,  parse_precedence_term       },
	['*']                    = { 0,              parse_binary,  parse_precedence_factor     },
	['/']                    = { 0,              parse_binary,  parse_precedence_factor     },
	['%']                    = { 0,              parse_binary,  parse_precedence_factor     },
	['<']                    = { 0,              parse_binary,  parse_precedence_comparison },
	['>']                    = { 0,              parse_binary,  parse_precedence_comparison },
	[token_type_leq]         = { 0,              parse_binary,  parse_precedence_comparison },
	[token_type_geq]         = { 0,              parse_binary,  parse_precedence_comparison },
	[token_type_or]          = { 0,              parse_binary,  parse_precedence_or         },
	[token_type_and]         = { 0,              parse_binary,  parse_precedence_and        },
	[token_type_eq]          = { 0,              parse_binary,  parse_precedence_equality   },
	[token_type_neq]         = { 0,              parse_binary,  parse_precedence_equality   },
	[token_type_in]          = { 0,              parse_binary,  parse_precedence_equality   },
	[token_type_not_in]      = { 0,              parse_binary,  parse_precedence_equality   },
	['.']                    = { 0,              parse_member,  parse_precedence_call       },
	['?']                    = { 0,              parse_ternary, parse_precedence_assignment },
	[token_type_not]         = { parse_unary,    0,             0                           },
	[token_type_func]        = { parse_func,     0,             parse_precedence_assignment },
};

static const struct parse_behavior parse_behavior_base = {
	.advance = parse_advance,
	.parse_stmt = parse_stmt,
	.parse_list = parse_list,
};
// clang-format on

struct node *
parse(struct workspace *wk, const struct source *src, enum vm_compile_mode mode)
{
	struct parser p;
	return parse_impl(wk, src, mode, &parse_behavior_base, parse_rules_base, &p);
}

struct node *
parse_fmt(struct workspace *wk, const struct source *src, enum vm_compile_mode mode, obj *raw_blocks)
{
	struct parse_rule parse_rules[ARRAY_LEN(parse_rules_base)];
	memcpy(parse_rules, parse_rules_base, sizeof(parse_rules_base));
	parse_rules['('].prefix = parse_grouping_fmt;

	struct parser p;
	struct node *n = parse_impl(wk, src, mode, &parse_behavior_base, parse_rules, &p);

	*raw_blocks = p.lexer.fmt.raw_blocks;

	/* LO("raw blocks: %o\n", *raw_blocks); */

	return n;
}

/******************************************************************************
* cmake
******************************************************************************/

static void
cm_parse_advance(struct parser *p)
{
	struct lexer prev_lexer;

	p->previous = p->current;
	p->fmt.previous = p->fmt.current;

	bool relex = false;

relex:
	prev_lexer = p->lexer;

	switch (p->cm_mode) {
	case cm_parse_mode_conditional: p->lexer.cm_mode = cm_lexer_mode_conditional; break;
	default: break;
	}

	cm_lexer_next(&p->lexer, &p->current);
	p->lexer.cm_mode = cm_lexer_mode_default;

	while (p->current.type == token_type_error) {
		parse_error(p, &p->current.location, "%s", get_cstr(p->wk, p->current.data.str));
		p->err.unwinding = false;
		prev_lexer = p->lexer;
		cm_lexer_next(&p->lexer, &p->current);
	}

	if (p->current.type == token_type_eof) {
		if (p->previous.type != token_type_eol) {
			// simulate a eol at the end of the file even if we
			// didn't get one
			p->current.type = token_type_eol;
		}
	}

	if (!relex) {
		if (p->cm_mode == cm_parse_mode_command && p->current.type == token_type_identifier) {
			struct lexer lexer_peek = p->lexer;
			struct token next;

			cm_lexer_next(&lexer_peek, &next);

			if (next.type == '(') {
				p->lexer = prev_lexer;
				p->lexer.cm_mode = cm_lexer_mode_command;
				relex = true;
				goto relex;
			}
		}
	}

	if (p->cm_mode == cm_parse_mode_conditional && p->current.type == token_type_string) {
		p->current.type = token_type_identifier;
	} else if (p->cm_mode == cm_parse_mode_command_args && p->current.type == token_type_identifier) {
		p->current.type = token_type_string;
	}

	/* LL("%d, previous: %s, current: ", p->cm_mode, token_to_s(p->wk, &p->previous)); */
	/* log_plain("%s\n", token_to_s(p->wk, &p->current)); */

	/* obj_fprintf(p->wk, log_file(), "%o, %o\n", p->fmt.previous.ws, p->fmt.current.ws); */

	/* list_line_range(p->src, p->current.location, 0); */
}

static struct node *
cm_parse_id(struct parser *p, bool assignment_allowed)
{
	if (p->cm_mode == cm_parse_mode_conditional) {
		return make_node_t(p, node_type_maybe_id);
	} else {
		return make_node_t(p, node_type_id);
	}
}

static struct node *
cm_parse_string(struct parser *p, bool assignment_allowed)
{
	return make_node_t(p, node_type_string);
}

static struct node *
cm_parse_with_mode(struct parser *p, enum cm_parse_mode mode, struct node *(parse_fun)(struct parser *))
{
	stack_push(&p->wk->stack, p->cm_mode, mode);
	struct node *n = parse_fun(p);
	stack_pop(&p->wk->stack, p->cm_mode);
	return n;
}

static struct node *
cm_parse_ignored_list(struct parser *p)
{
	parse_expect(p, '(');
	p->behavior.parse_list(p, node_type_def_args, ')');
	return 0;
}

static struct node *
cm_parse_stmt(struct parser *p, enum parse_stmt_flags flags)
{
	struct node *n;
	if (parse_accept(p, token_type_if)) {
		struct node *parent;
		parent = n = make_node_t(p, node_type_if);
		while (true) {
			n->l = make_node_t(p, node_type_list);
			n->l->l = p->previous.type == token_type_else ?
					  cm_parse_ignored_list(p) :
					  cm_parse_with_mode(p, cm_parse_mode_conditional, parse_expr);
			parse_expect(p, token_type_eol);
			n->l->r = parse_block(
				p, (enum token_type[]){ token_type_elif, token_type_else, token_type_endif }, 3, 0);

			if (!(parse_accept(p, token_type_elif) || parse_accept(p, token_type_else))) {
				break;
			}

			n = n->r = make_node_t(p, node_type_if);
		}

		parse_expect(p, token_type_endif);
		cm_parse_ignored_list(p);

		n = parent;
	} else {
		n = parse_expr(p);
	}

	if (p->err.unwinding) {
		while (p->err.unwinding && p->current.type != token_type_eof) {
			switch (p->current.type) {
			case token_type_eol: p->err.unwinding = false; break;
			default: break;
			}

			if (p->err.unwinding) {
				p->behavior.advance(p);
			}
		}
	} else {
		parse_expect(p, token_type_eol);
	}

	return n;
}

static struct node *
cm_parse_list(struct parser *p, enum node_type t, enum token_type end)
{
	struct node *n = make_node_t(p, t), *res = n;
	uint32_t len = 0;

	while (p->current.type != end && p->current.type != token_type_eol) {
		n->l = parse_expr(p);
		++len;
		if (p->current.type != end) {
			n = n->r = make_node_t(p, node_type_list);
		}
	}

	parse_expect(p, end);

	res->data.len.args = len;
	return res;
}

static struct node *
cm_parse_call(struct parser *p, struct node *l, bool assignment_allowed)
{
	struct node *n;

	if (p->current.type == token_type_identifier) {
		p->current.type = token_type_string;
	}

	stack_push(&p->wk->stack, p->cm_mode, cm_parse_mode_command_args);

	n = make_node_t(p, node_type_call);
	n->r = l;
	n->l = p->behavior.parse_list(p, node_type_args, ')');

	/* n->r->data.str = make_strf(p->wk, "cm_%s", get_cstr(p->wk, n->r->data.str)); */

	stack_pop(&p->wk->stack, p->cm_mode);

	n->location = source_location_merge(l->location, p->previous.location);

	if (n->r->type == node_type_id) {
		n->r->type = node_type_id_lit;
	}
	return n;
}

struct node *
cm_parse(struct workspace *wk, const struct source *src)
{
	struct parse_behavior behavior = {
		.advance = cm_parse_advance,
		.parse_stmt = cm_parse_stmt,
		.parse_list = cm_parse_list,
	};

	// clang-format off
	struct parse_rule parse_rules[token_type_count] = {
		[token_type_identifier] = { cm_parse_id, 0, 0 },
		[token_type_string] = { cm_parse_string, 0, 0 },
		['('] = { parse_grouping, cm_parse_call, parse_precedence_call },
		['<']                   = { 0,              parse_binary,  parse_precedence_comparison },
		['>']                   = { 0,              parse_binary,  parse_precedence_comparison },
		[token_type_leq]        = { 0,              parse_binary,  parse_precedence_comparison },
		[token_type_geq]        = { 0,              parse_binary,  parse_precedence_comparison },
		[token_type_or]         = { 0,              parse_binary,  parse_precedence_or         },
		[token_type_and]        = { 0,              parse_binary,  parse_precedence_and        },
		[token_type_eq]         = { 0,              parse_binary,  parse_precedence_equality   },
		[token_type_neq]        = { 0,              parse_binary,  parse_precedence_equality   },
		[token_type_in]         = { 0,              parse_binary,  parse_precedence_equality   },
		[token_type_not_in]     = { 0,              parse_binary,  parse_precedence_equality   },
	};
	// clang-format on

	struct parser p;
	struct node *n = parse_impl(wk, src, 0, &behavior, parse_rules, &p);
	/* print_ast(wk, n); */
	return n;
}
