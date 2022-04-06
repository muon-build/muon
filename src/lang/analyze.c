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
#include "lang/analyze.h"
#include "lang/eval.h"
#include "lang/interpreter.h"
#include "lang/parser.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"

static const char *
inspect_typeinfo(struct workspace *wk, obj t)
{
	struct obj_typeinfo *ti = get_obj_typeinfo(wk, t);
	obj expected_types;
	make_obj(wk, &expected_types, obj_array);
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(typemap); ++i) {
		if ((ti->type & typemap[i].tc) != typemap[i].tc) {
			continue;
		}

		obj_array_push(wk, expected_types, make_str(wk, obj_type_to_s(typemap[i].type)));
	}

	obj typestr;
	obj_array_join(wk, false, expected_types, make_str(wk, "|"), &typestr);
	return get_cstr(wk, typestr);
}

static uint32_t
make_typeinfo(struct workspace *wk, uint32_t t, uint32_t sub_t)
{
	assert(t & obj_typechecking_type_tag);
	if (sub_t) {
		assert(sub_t & obj_typechecking_type_tag);
	}

	obj res;
	make_obj(wk, &res, obj_typeinfo);
	struct obj_typeinfo *type = get_obj_typeinfo(wk, res);
	type->type = t;
	type->subtype = sub_t;
	return res;
}

typedef bool ((analyze_for_each_type_cb)(struct workspace *wk, void *ctx, uint32_t n_id, enum obj_type t, obj *res));

static uint32_t
obj_type_to_tc_type(enum obj_type t)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(typemap); ++i) {
		if (t == typemap[i].type) {
			return typemap[i].tc;
		}
	}

	assert(false && "unreachable");
}

static void
merge_types(struct workspace *wk, struct obj_typeinfo *a, obj r)
{
	enum obj_type t = get_obj_type(wk, r);
	if (t == obj_typeinfo) {
		a->type |= get_obj_typeinfo(wk, r)->type;
	} else {
		a->type |= obj_type_to_tc_type(t);
	}
}

static bool
analyze_for_each_type(struct workspace *wk, void *ctx, uint32_t n_id,
	uint32_t t, analyze_for_each_type_cb cb, obj *res)
{
	bool ok = false;
	uint32_t i;
	obj r;

	if (t & obj_typechecking_type_tag) {
		struct obj_typeinfo res_t = { 0 };

		for (i = 0; i < ARRAY_LEN(typemap); ++i) {
			if ((t & typemap[i].tc) == typemap[i].tc) {
				ok |= cb(wk, ctx, n_id, typemap[i].type, &r);
				merge_types(wk, &res_t, r);
			}
		}

		make_obj(wk, res, obj_typeinfo);
		*get_obj_typeinfo(wk, *res) = res_t;
		L("result: '%s'", inspect_typeinfo(wk, *res));
	} else {
		L("calling");
		ok |= cb(wk, ctx, n_id, t, res);
		L("result: %d|%s", *res, obj_type_to_s(get_obj_type(wk, *res)));
	}

	return ok;
}

static bool analyze_chained(struct workspace *wk, uint32_t node_id, obj l_id, obj *res);

static bool
analyze_method(struct workspace *wk, uint32_t node_id, obj l_id, obj *res)
{
	obj tmp = 0;
	struct node *n = get_node(wk->ast, node_id);

	if (!builtin_run(wk, true, l_id, node_id, &tmp)) {
		return false;
	}

	if (n->chflg & node_child_d) {
		return analyze_chained(wk, n->d, tmp, res);
	} else {
		*res = tmp;
		return true;
	}
}

struct analyze_ctx {
	obj r;
};

static bool
analyze_index(struct workspace *wk, void *_ctx, uint32_t n_id, enum obj_type lhs, obj *res)
{
	L("calling analyze index for type %s", obj_type_to_s(lhs));
	struct analyze_ctx *ctx = _ctx;
	struct node *n = get_node(wk->ast, n_id);

	obj tmp = 0;

	switch (lhs) {
	case obj_disabler:
		*res = disabler_id;
		return true;
	case obj_array: {
		if (!typecheck(wk, n->r, ctx->r, obj_number)) {
			return false;
		}

		tmp = make_typeinfo(wk, tc_string, 0);
		break;
	}
	case obj_dict: {
		if (!typecheck(wk, n->r, ctx->r, obj_string)) {
			return false;
		}

		tmp = make_typeinfo(wk, tc_string, 0);
		break;
	}
	case obj_custom_target: {
		if (!typecheck(wk, n->r, ctx->r, obj_number)) {
			return false;
		}

		tmp = make_typeinfo(wk, tc_file, 0);
		break;
	}
	case obj_string: {
		if (!typecheck(wk, n->r, ctx->r, obj_number)) {
			return false;
		}

		tmp = make_typeinfo(wk, tc_string, 0);
		break;
	}
	default:
		interp_error(wk, n->r, "index unsupported for %s", obj_type_to_s(lhs));
		return false;
	}

	if (n->chflg & node_child_d) {
		return analyze_chained(wk, n->d, tmp, res);
	} else {
		*res = tmp;
		return true;
	}
}

#if 0
static bool
analyze_index(struct workspace *wk, void *_ctx, uint32_t n_id, enum obj_type t, obj *res)
{
	struct analyze_ctx *ctx = _ctx;

	ctx->lhs = t;
	return analyze_for_each_type(wk, ctx, node_id, ctx->rhs, analyze_index_, res);
}
#endif

static bool
analyze_chained(struct workspace *wk, uint32_t node_id, obj l_id, obj *res)
{
	struct node *n = get_node(wk->ast, node_id);

	switch (n->type) {
	case node_method:
		return analyze_method(wk, node_id, l_id, res);
	case node_index: {
		obj r;
		if (!wk->interp_node(wk, n->r, &r)) {
			return false;
		}

		struct analyze_ctx ctx = {
			.r = r,
		};

		return analyze_for_each_type(wk, &ctx, node_id, get_obj_type(wk, l_id), analyze_index, res);
	}
	/* return analyze_index(wk, n, l_id, res); */
	default:
		assert(false && "unreachable");
		break;
	}

	return false;
}

#if 0
static bool
interp_u_minus(struct workspace *wk, struct node *n, obj *res)
{
	obj l_id;

	if (!interp_node(wk, n->l, &l_id)) {
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

	if (!interp_node(wk, nl, &l_id)
	    || !interp_node(wk, nr, &r_id)) {
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

		obj_dict_merge(wk, l_id, r_id, res);
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

	if (!interp_node(wk, n->r, &rhs)) {
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

	if (!interp_node(wk, n->l, &l)) {
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

	if (!interp_node(wk, n->l, &key)) {
		return false;
	}

	if (!typecheck(wk, n->l, key, obj_string)) {
		return false;
	}

	if (!interp_node(wk, n->r, &value)) {
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

	if (!interp_node(wk, n->l, &obj_l)) {
		return false;
	}

	if (have_r && get_node(wk->ast, n->r)->type != node_empty) {
		if (!interp_node(wk, n->r, &obj_r)) {
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

	if (!interp_node(wk, n->l, &obj_l_id)) {
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

	if (!interp_node(wk, n->l, &obj_l_id)) {
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

	if (!interp_node(wk, n->r, &obj_r_id)) {
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

	if (!interp_node(wk, n->l, &obj_l_id)) {
		return false;
	} else if (!interp_node(wk, n->r, &obj_r_id)) {
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
	if (!interp_node(wk, n->l, &cond_id)) {
		return false;
	} else if (cond_id == disabler_id) {
		*res = disabler_id;
		return true;
	} else if (!typecheck(wk, n->l, cond_id, obj_bool)) {
		return false;
	}

	uint32_t node = get_obj_bool(wk, cond_id) ? n->r : n->c;

	return interp_node(wk, node, res);
}

static bool
interp_if(struct workspace *wk, struct node *n, obj *res)
{
	bool cond;

	switch ((enum if_type)n->subtype) {
	case if_normal: {
		obj cond_id;
		if (!interp_node(wk, n->l, &cond_id)) {
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
		if (!interp_node(wk, n->r, res)) {
			return false;
		}
	} else if (n->chflg & node_child_c) {
		if (!interp_node(wk, n->c, res)) {
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

	if (!interp_node(wk, n->r, &iterable)) {
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

	if (!interp_node(wk, n->l, &l_id)) {
		return false;
	}

	if (!coerce_string(wk, n->l, l_id, res)) {
		return false;
	}

	return true;
}
#endif

bool
analyze_node(struct workspace *wk, uint32_t n_id, obj *res)
{
	bool ret = false;
	*res = 0;

	struct node *n = get_node(wk->ast, n_id);

	L("analyze '%s'", node_to_s(n));

	wk->stack_depth = 0;

	if (wk->loop_ctl) {
		--wk->stack_depth;
		return true;
	}

	switch (n->type) {
	/* literals */
	case node_dict:
	case node_array:
	case node_string:
	case node_number:
	case node_bool:
	case node_block:
		ret = interp_node(wk, n_id, res);
		break;
	case node_id:
		if (!get_obj_id(wk, n->dat.s, res, wk->cur_project)) {
			interp_error(wk, n_id, "undefined object");
			ret = false;
			break;
		}
		ret = true;
		break;

	/* control flow */
	case node_if:
		/* ret = interp_if(wk, n, res); */
		break;
	case node_foreach:
		/* ret = interp_foreach(wk, n, res); */
		break;
	case node_continue:
		if (!wk->loop_depth) {
			LOG_E("continue outside loop");
			ret = false;
			break;
		}
		wk->loop_ctl = loop_continuing;
		ret = true;
		break;
	case node_break:
		if (!wk->loop_depth) {
			LOG_E("break outside loop");
			ret = false;
			break;
		}
		wk->loop_ctl = loop_breaking;
		ret = true;
		break;

	/* functions */
	case node_function:
		break;
	case node_method:
	case node_index: {
		obj l_id;
		assert(n->chflg & node_child_l);

		if (!analyze_node(wk, n->l, &l_id)) {
			ret = false;
			break;
		}

		ret = analyze_chained(wk, n_id, l_id, res);
		break;
	}

	/* assignment */
	case node_assignment:
		ret = interp_node(wk, n_id, res);
		break;

	/* comparison stuff */
	case node_not:
		/* ret = interp_not(wk, n, res); */
		break;
	case node_and:
	case node_or:
		/* ret = interp_andor(wk, n, res); */
		break;
	case node_comparison:
		/* ret = interp_comparison(wk, n, res); */
		break;
	case node_ternary:
		/* ret = interp_ternary(wk, n, res); */
		break;

	/* math */
	case node_u_minus:
		/* ret = interp_u_minus(wk, n, res); */
		break;
	case node_arithmetic:
		/* ret = interp_arithmetic(wk, n_id, n->subtype, false, n->l, n->r, res); */
		break;
	case node_plusassign:
		/* ret = interp_plusassign(wk, n_id, res); */
		break;

	/* special */
	case node_stringify:
		/* ret = interp_stringify(wk, n, res); */
		break;
	case node_empty:
		ret = true;
		break;

	/* never valid */
	case node_foreach_args:
	case node_argument:
	case node_paren:
	case node_empty_line:
	case node_null:
		assert(false && "unreachable");
		break;
	}

	--wk->stack_depth;
	return ret;
}

bool
do_analyze(void)
{
	bool res = false;;
	struct workspace wk;
	workspace_init(&wk);

	wk.interp_node = analyze_node;

	if (!workspace_setup_dirs(&wk, "dummy", "argv0", false)) {
		goto err;
	}

	uint32_t project_id;
	res = eval_project(&wk, NULL, wk.source_root, wk.build_root, &project_id);
err:
	workspace_destroy(&wk);
	return res;
}
