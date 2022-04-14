#include "posix.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "data/hash.h"
#include "error.h"
#include "functions/common.h"
#include "lang/eval.h"
#include "lang/interpreter.h"
#include "lang/parser.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"

__attribute__ ((format(printf, 3, 4)))
void
interp_error(struct workspace *wk, uint32_t n_id, const char *fmt, ...)
{
	struct node *n = get_node(wk->ast, n_id);

	va_list args;
	va_start(args, fmt);

	static char buf[BUF_SIZE_4k];
	obj_vsnprintf(wk, buf, BUF_SIZE_4k, fmt, args);
	va_end(args);

	error_message(wk->src, n->line, n->col, buf);
}

bool
bounds_adjust(struct workspace *wk, uint32_t len, int64_t *i)
{
	if (*i < 0) {
		*i += len;
	}

	return *i < len;
}

bool
boundscheck(struct workspace *wk, uint32_t n_id, uint32_t len, int64_t *i)
{
	if (!bounds_adjust(wk, len, i)) {
		interp_error(wk, n_id, "index %" PRId64 " out of bounds", *i);
		return false;
	}

	return true;
}

bool
rangecheck(struct workspace *wk, uint32_t n_id, int64_t min, int64_t max, int64_t n)
{
	if (n < min || n > max) {
		interp_error(wk, n_id, "number %" PRId64 " out of bounds (%" PRId64 ", %" PRId64 ")", n, min, max);
		return false;
	}

	return true;
}

bool
typecheck_simple_err(struct workspace *wk, obj o, enum obj_type type)
{
	enum obj_type got = get_obj_type(wk, o);

	if (type != obj_any && got != type) {
		LOG_E("expected type %s, got %s", obj_type_to_s(type), obj_type_to_s(got));
		return false;
	}

	return true;
}

const char *
typechecking_type_to_s(struct workspace *wk, enum obj_typechecking_type t)
{
	obj expected_types;
	make_obj(wk, &expected_types, obj_array);
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(typemap); ++i) {
		if ((t & typemap[i].tc) != typemap[i].tc) {
			continue;
		}

		obj_array_push(wk, expected_types, make_str(wk, obj_type_to_s(typemap[i].type)));
	}

	obj typestr;
	obj_array_join(wk, false, expected_types, make_str(wk, "|"), &typestr);
	return get_cstr(wk, typestr);
}

static bool
typecheck_typechecking_type(struct workspace *wk, uint32_t n_id,
	enum obj_type got, enum obj_typechecking_type type, const char *fmt)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(typemap); ++i) {
		if ((type & typemap[i].tc) != typemap[i].tc) {
			continue;
		}

		if (typemap[i].type == got) {
			return true;
		}
	}
	return false;
}

bool
typecheck_custom(struct workspace *wk, uint32_t n_id, obj obj_id, enum obj_type type, const char *fmt)
{
	enum obj_type got = get_obj_type(wk, obj_id);

	if (got == obj_typeinfo) {
		struct obj_typeinfo *ti = get_obj_typeinfo(wk, obj_id);
		got = ti->type;

		uint32_t i;
		for (i = 0; i < ARRAY_LEN(typemap); ++i) {
			if ((got & typemap[i].tc) != typemap[i].tc) {
				continue;
			}

			if ((type & obj_typechecking_type_tag)) {
				if (typecheck_typechecking_type(wk, n_id, typemap[i].type,
					(enum obj_typechecking_type)type, fmt)) {
					return true;
				}
			} else if (type == obj_any || typemap[i].type == type) {
				return true;
			}
		}

		if (fmt) {
			if ((type & obj_typechecking_type_tag)) {
				interp_error(wk, n_id, fmt,
					typechecking_type_to_s(wk, (enum obj_typechecking_type)type),
					typechecking_type_to_s(wk, (enum obj_typechecking_type)got));
			} else {
				interp_error(wk, n_id, fmt,
					obj_type_to_s(type),
					typechecking_type_to_s(wk, (enum obj_typechecking_type)got));
			}
		}
		return false;
	} else if ((type & obj_typechecking_type_tag)) {
		if (!typecheck_typechecking_type(wk, n_id, got, (enum obj_typechecking_type)type, fmt)) {
			if (fmt) {
				interp_error(wk, n_id, fmt,
					typechecking_type_to_s(wk, (enum obj_typechecking_type)type),
					obj_type_to_s(got));
			}
			return false;
		}
	} else {
		if (type != obj_any && got != type) {
			if (fmt) {
				interp_error(wk, n_id, fmt, obj_type_to_s(type), obj_type_to_s(got));
			}
			return false;
		}
	}

	return true;
}

bool
typecheck(struct workspace *wk, uint32_t n_id, obj obj_id, enum obj_type type)
{
	return typecheck_custom(wk, n_id, obj_id, type, "expected type %s, got %s");
}

struct typecheck_iter_ctx {
	uint32_t err_node;
	enum obj_type t;
};

static enum iteration_result
typecheck_array_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct typecheck_iter_ctx *ctx = _ctx;

	if (!typecheck_custom(wk, ctx->err_node, val, ctx->t, "expected type %s, got %s")) {
		return ir_err;
	}

	return ir_cont;
}

bool
typecheck_array(struct workspace *wk, uint32_t n_id, obj arr, enum obj_type type)
{
	if (!typecheck(wk, n_id, arr, obj_array)) {
		return false;
	}

	return obj_array_foreach(wk, arr, &(struct typecheck_iter_ctx) {
		.err_node = n_id,
		.t = type,
	}, typecheck_array_iter);
}

static enum iteration_result
typecheck_dict_iter(struct workspace *wk, void *_ctx, obj key, obj val)
{
	struct typecheck_iter_ctx *ctx = _ctx;

	if (!typecheck_custom(wk, ctx->err_node, val, ctx->t, "expected type %s, got %s")) {
		return ir_err;
	}

	return ir_cont;
}

bool
typecheck_dict(struct workspace *wk, uint32_t n_id, obj dict, enum obj_type type)
{
	if (!typecheck(wk, n_id, dict, obj_dict)) {
		return false;
	}

	return obj_dict_foreach(wk, dict, &(struct typecheck_iter_ctx) {
		.err_node = n_id,
		.t = type,
	}, typecheck_dict_iter);
}

static bool interp_chained(struct workspace *wk, uint32_t node_id, obj l_id, obj *res);

static bool
interp_method(struct workspace *wk, uint32_t node_id, obj l_id, obj *res)
{
	obj tmp = 0;
	struct node *n = get_node(wk->ast, node_id);

	if (!builtin_run(wk, true, l_id, node_id, &tmp)) {
		return false;
	}

	if (n->chflg & node_child_d) {
		return interp_chained(wk, n->d, tmp, res);
	} else {
		*res = tmp;
		return true;
	}
}

static bool
interp_index(struct workspace *wk, struct node *n, obj l_id, obj *res)
{
	obj r_id;
	obj tmp = 0;

	if (!wk->interp_node(wk, n->r, &r_id)) {
		return false;
	}

	enum obj_type t = get_obj_type(wk, l_id);

	switch (t) {
	case obj_disabler:
		*res = disabler_id;
		return true;
	case obj_array: {
		if (!typecheck(wk, n->r, r_id, obj_number)) {
			return false;
		}

		int64_t i = get_obj_number(wk, r_id);

		if (!boundscheck(wk, n->r, get_obj_array(wk, l_id)->len, &i)) {
			return false;
		}

		obj_array_index(wk, l_id, i, &tmp);
		break;
	}
	case obj_dict: {
		if (!typecheck(wk, n->r, r_id, obj_string)) {
			return false;
		}

		if (!obj_dict_index(wk, l_id, r_id, &tmp)) {
			interp_error(wk, n->r, "key not in dictionary: %o", r_id);
			return false;
		}
		break;
	}
	case obj_custom_target: {
		if (!typecheck(wk, n->r, r_id, obj_number)) {
			return false;
		}

		int64_t i = get_obj_number(wk, r_id);

		struct obj_custom_target *tgt = get_obj_custom_target(wk, l_id);
		struct obj_array *arr = get_obj_array(wk, tgt->output);

		if (!boundscheck(wk, n->r, arr->len, &i)) {
			return false;
		}

		obj_array_index(wk, tgt->output, i, &tmp);
		break;
	}
	case obj_string: {
		if (!typecheck(wk, n->r, r_id, obj_number)) {
			return false;
		}

		int64_t i = get_obj_number(wk, r_id);

		const struct str *s = get_str(wk, l_id);
		if (!boundscheck(wk, n->r, s->len, &i)) {
			return false;
		}

		tmp = make_strn(wk, &s->s[i], 1);
		break;
	}
	default:
		interp_error(wk, n->r, "index unsupported for %s", obj_type_to_s(t));
		return false;
	}

	if (n->chflg & node_child_d) {
		return interp_chained(wk, n->d, tmp, res);
	} else {
		*res = tmp;
		return true;
	}
}

static bool
interp_chained(struct workspace *wk, uint32_t node_id, obj l_id, obj *res)
{
	struct node *n = get_node(wk->ast, node_id);

	switch (n->type) {
	case node_method:
		return interp_method(wk, node_id, l_id, res);
	case node_index:
		return interp_index(wk, n, l_id, res);
	default:
		assert(false && "unreachable");
		break;
	}

	return false;
}

static bool
interp_u_minus(struct workspace *wk, struct node *n, obj *res)
{
	obj l_id;

	if (!wk->interp_node(wk, n->l, &l_id)) {
		return false;
	} else if (l_id == disabler_id) {
		*res = disabler_id;
		return true;
	} else if (!typecheck(wk, n->l, l_id, obj_number)) {
		return false;
	}

	make_obj(wk, res, obj_number);
	set_obj_number(wk, *res, -get_obj_number(wk, l_id));
	return true;
}

static bool
interp_arithmetic(struct workspace *wk, uint32_t err_node,
	enum arithmetic_type type, bool plusassign, uint32_t nl, uint32_t nr,
	obj *res)
{
	obj l_id, r_id;

	if (!wk->interp_node(wk, nl, &l_id)
	    || !wk->interp_node(wk, nr, &r_id)) {
		return false;
	}

	if (l_id == disabler_id || r_id == disabler_id) {
		*res = disabler_id;
		return true;
	}

	switch (get_obj_type(wk, l_id)) {
	case obj_string: {
		obj str;

		if (!typecheck_custom(wk, nr, r_id, obj_string, "unsupported operator for %s and %s")) {
			return false;
		}

		switch (type) {
		case arith_add:
			str = str_join(wk, l_id, r_id);
			break;
		case arith_div: {
			char buf[PATH_MAX];

			const struct str *ss1 = get_str(wk, l_id),
					 *ss2 = get_str(wk, r_id);

			if (str_has_null(ss1)) {
				interp_error(wk, nl, "%o is an invalid path", l_id);
				return false;
			}

			if (str_has_null(ss2)) {
				interp_error(wk, nr, "%o is an invalid path", r_id);
				return false;
			}

			if (!path_join(buf, PATH_MAX, ss1->s, ss2->s)) {
				return false;
			}

			str = make_str(wk, buf);
			break;
		}
		default:
			goto err1;
		}

		*res = str;
		break;
	}
	case obj_number: {
		int64_t num, l, r;

		if (!typecheck_custom(wk, nr, r_id, obj_number, "unsupported operator for %s and %s")) {
			return false;
		}

		l = get_obj_number(wk, l_id);
		r = get_obj_number(wk, r_id);

		switch (type) {
		case arith_add:
			num = l + r;
			break;
		case arith_div:
			if (!r) {
				interp_error(wk, nr, "divide by 0");
				return false;
			}
			num = l / r;
			break;
		case arith_sub:
			num = l - r;
			break;
		case arith_mod:
			if (!r) {
				interp_error(wk, nr, "divide by 0");
				return false;
			}
			num = l % r;
			break;
		case arith_mul:
			num = l * r;
			break;
		default:
			assert(false);
			return false;
		}

		make_obj(wk, res, obj_number);
		set_obj_number(wk, *res, num);
		break;
	}
	case obj_array: {
		switch (type) {
		case arith_add:
			if (plusassign) {
				*res = l_id;
			} else {
				obj_array_dup(wk, l_id, res);
			}

			if (get_obj_type(wk, r_id) == obj_array) {
				obj_array_extend(wk, *res, r_id);
			} else {
				obj_array_push(wk, *res, r_id);
			}
			return true;
		default:
			goto err1;
		}
	}
	case obj_dict: {
		if (!typecheck_custom(wk, nr, r_id, obj_dict, "unsupported operator for %s and %s")) {
			return false;
		} else if (type != arith_add) {
			goto err1;
		}

		if (plusassign) {
			obj_dict_merge_nodup(wk, l_id, r_id);
			*res = l_id;
		} else {
			obj_dict_merge(wk, l_id, r_id, res);
		}

		break;
	}
	default:
		goto err1;
	}

	return true;
err1:
	assert(type < 5);
	interp_error(wk, err_node, "%s does not support %c", obj_type_to_s(get_obj_type(wk, l_id)), "+-%*/"[type]);
	return false;
}

static bool
interp_assign(struct workspace *wk, struct node *n, obj *_)
{
	obj rhs;

	if (!wk->interp_node(wk, n->r, &rhs)) {
		return false;
	}

	switch (get_obj_type(wk, rhs)) {
	case obj_environment:
	case obj_configuration_data: {
		obj cloned;
		if (!obj_clone(wk, wk, rhs, &cloned)) {
			return false;
		}

		rhs = cloned;
		break;
	}
	case obj_array: {
		obj dup;
		obj_array_dup(wk, rhs, &dup);
		rhs = dup;
	}
	default:
		break;
	}

	hash_set_str(&current_project(wk)->scope, get_node(wk->ast, n->l)->dat.s, rhs);
	return true;
}

static bool
interp_plusassign(struct workspace *wk, uint32_t n_id, obj *_)
{
	struct node *n = get_node(wk->ast, n_id);

	obj rhs;
	if (!interp_arithmetic(wk, n_id, arith_add, true, n->l, n->r, &rhs)) {
		return false;
	}

	hash_set_str(&current_project(wk)->scope, get_node(wk->ast, n->l)->dat.s, rhs);
	return true;
}

static bool
interp_array(struct workspace *wk, uint32_t n_id, obj *res)
{
	obj l, r;

	struct node *n = get_node(wk->ast, n_id);

	if (n->type == node_empty) {
		make_obj(wk, res, obj_array);
		struct obj_array *arr = get_obj_array(wk, *res);
		arr->len = 0;
		arr->tail = *res;
		return true;
	}

	if (n->subtype == arg_kwarg) {
		interp_error(wk, n->l, "kwarg not valid in array constructor");
		return false;
	}

	bool have_c = n->chflg & node_child_c && get_node(wk->ast, n->c)->type != node_empty;

	if (!wk->interp_node(wk, n->l, &l)) {
		return false;
	}

	if (have_c) {
		if (!interp_array(wk, n->c, &r)) {
			return false;
		}
	}

	make_obj(wk, res, obj_array);
	struct obj_array *arr = get_obj_array(wk, *res);
	arr->val = l;

	if ((arr->have_next = have_c)) {
		struct obj_array *arr_r = get_obj_array(wk, r);

		arr->len = arr_r->len + 1;
		arr->tail = arr_r->tail;
		arr->next = r;
	} else {
		arr->len = 1;
		arr->tail = *res;
	}

	return true;
}

static bool
interp_dict(struct workspace *wk, uint32_t n_id, obj *res)
{
	obj key, value, tail;

	struct node *n = get_node(wk->ast, n_id);

	if (n->type == node_empty) {
		make_obj(wk, res, obj_dict);
		struct obj_dict *dict = get_obj_dict(wk, *res);
		dict->len = 0;
		dict->tail = *res;
		return true;
	}

	assert(n->type == node_argument);

	if (n->subtype != arg_kwarg) {
		interp_error(wk, n->l, "non-kwarg not valid in dict constructor");
		return false;
	}

	bool have_c = n->chflg & node_child_c && get_node(wk->ast, n->c)->type != node_empty;

	if (!wk->interp_node(wk, n->l, &key)) {
		return false;
	}

	if (!typecheck(wk, n->l, key, obj_string)) {
		return false;
	}

	if (!wk->interp_node(wk, n->r, &value)) {
		return false;
	}

	if (have_c) {
		if (!interp_dict(wk, n->c, &tail)) {
			return false;
		}
	}

	make_obj(wk, res, obj_dict);
	struct obj_dict *dict = get_obj_dict(wk, *res);
	dict->key = key;
	dict->val = value;

	if ((dict->have_next = have_c)) {
		struct obj_dict *dict_r = get_obj_dict(wk, tail);

		if (obj_dict_in(wk, tail, key)) {
			interp_error(wk, n->l, "key %o is duplicated", key);
			return false;
		}

		dict->len = dict_r->len + 1;
		dict->tail = dict_r->tail;
		dict->next = tail;
	} else {
		dict->len = 1;
		dict->tail = *res;
	}

	return true;
}

static bool
interp_block(struct workspace *wk, struct node *n, obj *res)
{
	bool have_r = n->chflg & node_child_r
		      && get_node(wk->ast, n->r)->type != node_empty;

	assert(n->type == node_block);

	obj obj_l, obj_r; // these return values are disregarded

	if (!wk->interp_node(wk, n->l, &obj_l)) {
		return false;
	}

	if (have_r && get_node(wk->ast, n->r)->type != node_empty) {
		if (!wk->interp_node(wk, n->r, &obj_r)) {
			return false;
		}

		*res = obj_r;
	} else {
		*res = obj_l;
	}

	return true;
}

static bool
interp_not(struct workspace *wk, struct node *n, obj *res)
{
	obj obj_l_id;

	if (!wk->interp_node(wk, n->l, &obj_l_id)) {
		return false;
	} else if (obj_l_id == disabler_id) {
		*res = disabler_id;
		return true;
	} else if (!typecheck(wk, n->l, obj_l_id, obj_bool)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, !get_obj_bool(wk, obj_l_id));
	return true;
}

static bool
interp_andor(struct workspace *wk, struct node *n, obj *res)
{
	obj obj_l_id, obj_r_id;

	if (!wk->interp_node(wk, n->l, &obj_l_id)) {
		return false;
	} else if (obj_l_id == disabler_id) {
		*res = disabler_id;
		return true;
	} else if (!typecheck(wk, n->l, obj_l_id, obj_bool)) {
		return false;
	}

	if (n->type == node_and && !get_obj_bool(wk, obj_l_id)) {
		make_obj(wk, res, obj_bool);
		set_obj_bool(wk, *res, false);
		return true;
	} else if (n->type == node_or && get_obj_bool(wk, obj_l_id)) {
		make_obj(wk, res, obj_bool);
		set_obj_bool(wk, *res, true);
		return true;
	}

	if (!wk->interp_node(wk, n->r, &obj_r_id)) {
		return false;
	} else if (obj_r_id == disabler_id) {
		*res = disabler_id;
		return true;
	} else if (!typecheck(wk, n->r, obj_r_id, obj_bool)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, get_obj_bool(wk, obj_r_id));
	return true;
}

static bool
interp_comparison(struct workspace *wk, struct node *n, obj *res)
{
	bool b;
	obj obj_l_id, obj_r_id;

	if (!wk->interp_node(wk, n->l, &obj_l_id)) {
		return false;
	} else if (!wk->interp_node(wk, n->r, &obj_r_id)) {
		return false;
	}

	if (obj_l_id == disabler_id || obj_r_id == disabler_id) {
		*res = disabler_id;
		return true;
	}

	switch ((enum comparison_type)n->subtype) {
	case comp_equal:
		b = obj_equal(wk, obj_l_id, obj_r_id);
		break;
	case comp_nequal:
		b = !obj_equal(wk, obj_l_id, obj_r_id);
		break;

	case comp_in:
	case comp_not_in:
		switch (get_obj_type(wk, obj_r_id)) {
		case obj_array:
			b = obj_array_in(wk, obj_r_id, obj_l_id);
			break;
		case obj_dict:
			if (!typecheck(wk, n->l, obj_l_id, obj_string)) {
				return false;
			}

			b = obj_dict_in(wk, obj_r_id, obj_l_id);
			break;
		default:
			interp_error(wk, n->r, "'in' not supported for %s", obj_type_to_s(get_obj_type(wk, obj_r_id)));
			return false;
		}

		if (n->subtype == comp_not_in) {
			b = !b;
		}
		break;

	case comp_lt:
	case comp_le:
	case comp_gt:
	case comp_ge: {
		if (!typecheck(wk, n->l, obj_l_id, obj_number)
		    || !typecheck(wk, n->r, obj_r_id, obj_number)) {
			return false;
		}

		int64_t n_a = get_obj_number(wk, obj_l_id),
			n_b = get_obj_number(wk, obj_r_id);

		switch (n->subtype) {
		case comp_lt:
			b = n_a < n_b;
			break;
		case comp_le:
			b = n_a <= n_b;
			break;
		case comp_gt:
			b = n_a > n_b;
			break;
		case comp_ge:
			b = n_a >= n_b;
			break;
		default: assert(false && "unreachable"); res = false;
		}
		break;
	}
	default: assert(false && "unreachable"); res = false;
		break;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, b);
	return true;
}

static bool
interp_ternary(struct workspace *wk, struct node *n, obj *res)
{
	obj cond_id;
	if (!wk->interp_node(wk, n->l, &cond_id)) {
		return false;
	} else if (cond_id == disabler_id) {
		*res = disabler_id;
		return true;
	} else if (!typecheck(wk, n->l, cond_id, obj_bool)) {
		return false;
	}

	uint32_t node = get_obj_bool(wk, cond_id) ? n->r : n->c;

	return wk->interp_node(wk, node, res);
}

static bool
interp_if(struct workspace *wk, struct node *n, obj *res)
{
	bool cond;

	switch ((enum if_type)n->subtype) {
	case if_normal: {
		obj cond_id;
		if (!wk->interp_node(wk, n->l, &cond_id)) {
			return false;
		} else if (cond_id == disabler_id) {
			*res = disabler_id;
			return true;
		} else if (!typecheck(wk, n->l, cond_id, obj_bool)) {
			return false;
		}

		cond = get_obj_bool(wk, cond_id);
		break;
	}
	case if_else:
		cond = true;
		break;
	default:
		assert(false);
		return false;
	}

	if (cond) {
		if (!wk->interp_node(wk, n->r, res)) {
			return false;
		}
	} else if (n->chflg & node_child_c) {
		if (!wk->interp_node(wk, n->c, res)) {
			return false;
		}
	} else {
		*res = 0;
	}

	return true;
}

struct interp_foreach_ctx {
	const char *id1, *id2;
	uint32_t block_node;
};

static enum iteration_result
interp_foreach_common(struct workspace *wk, struct interp_foreach_ctx *ctx)
{
	obj block_result;

	if (get_node(wk->ast, ctx->block_node)->type == node_empty) {
		return ir_done;
	}

	if (!interp_block(wk, get_node(wk->ast, ctx->block_node), &block_result)) {
		return ir_err;
	}

	switch (wk->loop_ctl) {
	case loop_continuing:
		wk->loop_ctl = loop_norm;
		break;
	case loop_breaking:
		wk->loop_ctl = loop_norm;
		return ir_done;
	case loop_norm:
		break;
	}

	return ir_cont;
}

static enum iteration_result
interp_foreach_dict_iter(struct workspace *wk, void *_ctx, obj k_id, obj v_id)
{
	struct interp_foreach_ctx *ctx = _ctx;

	hash_set_str(&current_project(wk)->scope, ctx->id1, k_id);
	hash_set_str(&current_project(wk)->scope, ctx->id2, v_id);

	return interp_foreach_common(wk, ctx);
}

static enum iteration_result
interp_foreach_arr_iter(struct workspace *wk, void *_ctx, obj v_id)
{
	struct interp_foreach_ctx *ctx = _ctx;

	hash_set_str(&current_project(wk)->scope, ctx->id1, v_id);

	return interp_foreach_common(wk, ctx);
}

static bool
interp_foreach(struct workspace *wk, struct node *n, obj *res)
{
	obj iterable;
	bool ret;

	if (!wk->interp_node(wk, n->r, &iterable)) {
		return false;
	}

	struct node *args = get_node(wk->ast, n->l);

	switch (get_obj_type(wk, iterable)) {
	case obj_array: {
		if (args->chflg & node_child_r) {
			interp_error(wk, n->l, "array foreach needs exactly one variable to set");
			return false;
		}

		struct interp_foreach_ctx ctx = {
			.id1 = get_node(wk->ast, args->l)->dat.s,
			.block_node = n->c,
		};

		++wk->loop_depth;
		wk->loop_ctl = loop_norm;
		ret = obj_array_foreach(wk, iterable, &ctx, interp_foreach_arr_iter);
		--wk->loop_depth;

		break;
	}
	case obj_dict: {
		if (!(args->chflg & node_child_r)) {
			interp_error(wk, n->l, "dict foreach needs exactly two variables to set");
			return false;
		}

		assert(get_node(wk->ast, get_node(wk->ast, args->r)->type == node_foreach_args));

		struct interp_foreach_ctx ctx = {
			.id1 = get_node(wk->ast, args->l)->dat.s,
			.id2 = get_node(wk->ast, get_node(wk->ast, args->r)->l)->dat.s,
			.block_node = n->c,
		};

		++wk->loop_depth;
		wk->loop_ctl = loop_norm;
		ret = obj_dict_foreach(wk, iterable, &ctx, interp_foreach_dict_iter);
		--wk->loop_depth;

		break;
	}
	default:
		interp_error(wk, n->r, "%s is not iterable", obj_type_to_s(get_obj_type(wk, iterable)));
		return false;
	}

	return ret;
}

static bool
interp_func(struct workspace *wk, uint32_t n_id, obj *res)
{
	obj tmp = 0;
	struct node *n = get_node(wk->ast, n_id);

	if (!builtin_run(wk, false, 0, n_id, &tmp)) {
		return false;
	}

	if (n->chflg & node_child_d) {
		return interp_chained(wk, n->d, tmp, res);
	} else {
		*res = tmp;
		return true;
	}
}

static bool
interp_stringify(struct workspace *wk, struct node *n, obj *res)
{
	obj l_id;

	if (!wk->interp_node(wk, n->l, &l_id)) {
		return false;
	}

	if (!coerce_string(wk, n->l, l_id, res)) {
		return false;
	}

	return true;
}

bool
interp_node(struct workspace *wk, uint32_t n_id, obj *res)
{
	static const uint32_t maximum_stack_depth = 1000;

	++wk->stack_depth;
	if (wk->stack_depth > maximum_stack_depth) {
		interp_error(wk, n_id, "stack overflow");
		--wk->stack_depth;
		return false;
	}

	bool ret = false;
	*res = 0;

	struct node *n = get_node(wk->ast, n_id);

	/* L("%s", node_to_s(n)); */
	if (wk->subdir_done) {
		return true;
	}

	if (wk->loop_ctl) {
		--wk->stack_depth;
		return true;
	}

	switch (n->type) {
	/* literals */
	case node_bool:
		make_obj(wk, res, obj_bool);
		set_obj_bool(wk, *res, n->subtype);
		ret = true;
		break;
	case node_string:
		*res = make_strn(wk, n->dat.s, n->subtype);
		ret = true;
		break;
	case node_array:
		ret = interp_array(wk, n->l, res);
		break;
	case node_dict:
		ret = interp_dict(wk, n->l, res);
		break;
	case node_id:
		if (!get_obj_id(wk, n->dat.s, res, wk->cur_project)) {
			interp_error(wk, n_id, "undefined object");
			ret = false;
			break;
		}
		ret = true;
		break;
	case node_number:
		make_obj(wk, res, obj_number);
		set_obj_number(wk, *res, n->dat.n);
		ret = true;
		break;

	/* control flow */
	case node_block:
		ret = interp_block(wk, n, res);
		break;
	case node_if:
		ret = interp_if(wk, n, res);
		break;
	case node_foreach:
		ret = interp_foreach(wk, n, res);
		break;
	case node_continue:
		assert(wk->loop_depth && "continue outside loop");
		wk->loop_ctl = loop_continuing;
		ret = true;
		break;
	case node_break:
		assert(wk->loop_depth && "break outside loop");
		wk->loop_ctl = loop_breaking;
		ret = true;
		break;

	/* functions */
	case node_function:
		ret = interp_func(wk, n_id, res);
		break;
	case node_method:
	case node_index: {
		obj l_id;
		assert(n->chflg & node_child_l);

		if (!wk->interp_node(wk, n->l, &l_id)) {
			ret = false;
			break;
		}

		ret = interp_chained(wk, n_id, l_id, res);
		break;
	}

	/* assignment */
	case node_assignment:
		ret = interp_assign(wk, n, res);
		break;

	/* comparison stuff */
	case node_not:
		ret = interp_not(wk, n, res);
		break;
	case node_and:
	case node_or:
		ret = interp_andor(wk, n, res);
		break;
	case node_comparison:
		ret = interp_comparison(wk, n, res);
		break;
	case node_ternary:
		ret = interp_ternary(wk, n, res);
		break;

	/* math */
	case node_u_minus:
		ret = interp_u_minus(wk, n, res);
		break;
	case node_arithmetic:
		ret = interp_arithmetic(wk, n_id, n->subtype, false, n->l, n->r, res);
		break;
	case node_plusassign:
		ret = interp_plusassign(wk, n_id, res);
		break;

	/* special */
	case node_stringify:
		ret = interp_stringify(wk, n, res);
		break;

	/* handled in other places */
	case node_foreach_args:
	case node_argument:
		assert(false && "unreachable");
		break;

	case node_empty:
		ret = true;
		break;

	/* never valid */
	case node_paren:
	case node_empty_line:
	case node_null:
		assert(false && "invalid node");
		break;
	}

	--wk->stack_depth;
	return ret;
}
