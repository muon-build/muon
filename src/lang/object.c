#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "lang/interpreter.h"
#include "lang/object.h"
#include "lang/parser.h"
#include "log.h"

const char *
obj_type_to_s(enum obj_type t)
{
	switch (t) {
	case obj_any: return "any";
	case obj_default: return "default";
	case obj_null: return "null";
	case obj_compiler: return "compiler";
	case obj_dependency: return "dependency";
	case obj_meson: return "meson";
	case obj_string: return "string";
	case obj_number: return "number";
	case obj_array: return "array";
	case obj_dict: return "dict";
	case obj_bool: return "bool";
	case obj_file: return "file";
	case obj_build_target: return "build_target";
	case obj_subproject: return "subproject";
	case obj_machine: return "machine";
	case obj_feature_opt: return "feature_opt";
	case obj_external_program: return "external_program";
	case obj_external_library: return "external_library";
	case obj_run_result: return "run_result";
	case obj_configuration_data: return "configuration_data";
	case obj_custom_target: return "custom_target";
	case obj_test: return "test";
	case obj_module: return "module";
	case obj_install_target: return "install_target";
	case obj_environment: return "environment";

	case obj_type_count:
	case ARG_TYPE_NULL:
	case ARG_TYPE_GLOB:
	case ARG_TYPE_ARRAY_OF:
		assert(false); return "uh oh";
	}

	assert(false && "unreachable");
	return NULL;
}

struct obj_equal_iter_ctx {
	obj other_container;
	uint32_t i;
};

static enum iteration_result
obj_equal_array_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_equal_iter_ctx *ctx = _ctx;
	obj r;

	obj_array_index(wk, ctx->other_container, ctx->i, &r);

	if (!obj_equal(wk, val, r)) {
		return ir_err;
	}

	++ctx->i;
	return ir_cont;
}

bool
obj_equal(struct workspace *wk, obj left, obj right)
{
	if (left == right) {
		return true;
	}

	struct obj *l = get_obj(wk, left),
		   *r = get_obj(wk, right);

	if (l->type != r->type) {
		return false;
	}

	switch (l->type) {
	case obj_string:
		return strcmp(wk_str(wk, l->dat.str), wk_str(wk, r->dat.str)) == 0;
	case obj_file:
		return strcmp(wk_str(wk, l->dat.file), wk_str(wk, r->dat.file)) == 0;
	case obj_number:
		return l->dat.num == r->dat.num;
	case obj_bool:
		return l->dat.boolean == r->dat.boolean;
	case obj_array: {
		struct obj_equal_iter_ctx ctx = {
			.other_container = right,
		};

		return l->dat.arr.len == r->dat.arr.len
		       && obj_array_foreach(wk, left, &ctx, obj_equal_array_iter);
	}
	break;
	case obj_dict:
		LOG_W("TODO: compare %s", obj_type_to_s(l->type));
		return false;
	default:
		return false;
	}
}

/*
 * arrays
 */

bool
obj_array_foreach(struct workspace *wk, obj arr, void *ctx, obj_array_iterator cb)
{
	assert(get_obj(wk, arr)->type == obj_array);

	if (!get_obj(wk, arr)->dat.arr.len) {
		return true;
	}

	while (true) {
		switch (cb(wk, ctx, get_obj(wk, arr)->dat.arr.val)) {
		case ir_cont:
			break;
		case ir_done:
			return true;
		case ir_err:
			return false;
		}

		if (!get_obj(wk, arr)->dat.arr.have_next) {
			break;
		}
		arr = get_obj(wk, arr)->dat.arr.next;
	}

	return true;
}

struct obj_array_foreach_flat_ctx {
	void *usr_ctx;
	obj_array_iterator cb;
};

static enum iteration_result
obj_array_foreach_flat_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_array_foreach_flat_ctx *ctx = _ctx;

	if (get_obj(wk, val)->type == obj_array) {
		if (!obj_array_foreach(wk, val, ctx, obj_array_foreach_flat_iter)) {
			return ir_err;
		} else {
			return ir_cont;
		}
	} else {
		return ctx->cb(wk, ctx->usr_ctx, val);
	}

	return ir_cont;
}

bool
obj_array_foreach_flat(struct workspace *wk, obj arr, void *usr_ctx, obj_array_iterator cb)
{
	struct obj_array_foreach_flat_ctx ctx = {
		.usr_ctx = usr_ctx,
		.cb = cb,
	};

	return obj_array_foreach(wk, arr, &ctx, obj_array_foreach_flat_iter);
}

void
obj_array_push(struct workspace *wk, obj arr, obj child)
{
	obj child_arr;
	struct obj *a, *tail, *c;

	if (!(a = get_obj(wk, arr))->dat.arr.len) {
		a->dat.arr.tail = arr;
		a->dat.arr.len = 1;
		a->dat.arr.val = child;
		return;
	}

	c = make_obj(wk, &child_arr, obj_array);
	c->dat.arr.val = child;

	a = get_obj(wk, arr);
	assert(a->type == obj_array);

	tail = get_obj(wk, a->dat.arr.tail);
	assert(tail->type == obj_array);
	assert(!tail->dat.arr.have_next);
	tail->dat.arr.have_next = true;
	tail->dat.arr.next = child_arr;

	a->dat.arr.tail = child_arr;
	++a->dat.arr.len;
}

struct obj_array_in_iter_ctx {
	obj l;
	bool res;
};

static enum iteration_result
obj_array_in_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct obj_array_in_iter_ctx *ctx = _ctx;

	if (obj_equal(wk, ctx->l, v)) {
		ctx->res = true;
		return ir_done;
	}

	return ir_cont;
}

bool
obj_array_in(struct workspace *wk, obj arr, obj val)
{
	struct obj_array_in_iter_ctx ctx = { .l = val };
	obj_array_foreach(wk, arr, &ctx, obj_array_in_iter);

	return ctx.res;
}

struct obj_array_index_iter_ctx { obj res, i, tgt; };

static enum iteration_result
obj_array_index_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct obj_array_index_iter_ctx *ctx = _ctx;

	if (ctx->i == ctx->tgt) {
		ctx->res = v;
		return ir_done;
	}

	++ctx->i;
	return ir_cont;
}

bool
obj_array_index(struct workspace *wk, obj arr, int64_t i, obj *res)
{
	struct obj_array_index_iter_ctx ctx = { .tgt = i };

	assert(i >= 0 && i < get_obj(wk, arr)->dat.arr.len);

	if (!obj_array_foreach(wk, arr, &ctx, obj_array_index_iter)) {
		LOG_E("obj_array_index failed");
		return false;
	}

	*res = ctx.res;
	return true;
}

struct obj_array_dup_ctx { obj *arr; };

static enum iteration_result
obj_array_dup_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct obj_array_dup_ctx *ctx = _ctx;

	obj_array_push(wk, *ctx->arr, v);

	return ir_cont;
}

bool
obj_array_dup(struct workspace *wk, obj arr, obj *res)
{
	struct obj_array_dup_ctx ctx = { .arr = res };

	make_obj(wk, res, obj_array);

	if (!obj_array_foreach(wk, arr, &ctx, obj_array_dup_iter)) {
		return false;
	}

	return true;
}

void
obj_array_extend(struct workspace *wk, obj arr, obj arr2)
{
	struct obj *a, *b, *tail;

	assert(get_obj(wk, arr)->type == obj_array
		&& get_obj(wk, arr2)->type == obj_array);

	if (!(b = get_obj(wk, arr2))->dat.arr.len) {
		return;
	}

	if (!(a = get_obj(wk, arr))->dat.arr.len) {
		*a = *b;
		return;
	}

	tail = get_obj(wk, a->dat.arr.tail);
	assert(tail->type == obj_array);
	assert(!tail->dat.arr.have_next);
	tail->dat.arr.have_next = true;
	tail->dat.arr.next = arr2;

	a->dat.arr.tail = b->dat.arr.tail;
	a->dat.arr.len += b->dat.arr.len;
}

struct obj_array_join_ctx {
	obj join, *res;
	uint32_t i, len;
};

static enum iteration_result
obj_array_join_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_array_join_ctx *ctx = _ctx;

	if (!typecheck_simple_err(wk, val, obj_string)) {
		return ir_err;
	}

	wk_str_app(wk, &get_obj(wk, *ctx->res)->dat.str, wk_objstr(wk, val));

	if (ctx->i < ctx->len - 1) {
		wk_str_app(wk, &get_obj(wk, *ctx->res)->dat.str, wk_str(wk, ctx->join));
	}

	++ctx->i;

	return ir_cont;
}

bool
obj_array_join(struct workspace *wk, obj arr, obj join, obj *res)
{
	make_obj(wk, res, obj_string)->dat.str = wk_str_push(wk, "");

	if (!typecheck_simple_err(wk, join, obj_string)) {
		return false;
	}

	struct obj_array_join_ctx ctx = {
		.join = get_obj(wk, join)->dat.str,
		.res = res,
		.len = get_obj(wk, arr)->dat.arr.len
	};

	return obj_array_foreach(wk, arr, &ctx, obj_array_join_iter);
}

void
obj_array_set(struct workspace *wk, obj arr, int64_t i, obj v)
{
	assert(get_obj(wk, arr)->type == obj_array);
	assert(i >= 0 && i < get_obj(wk, arr)->dat.arr.len);

	uint32_t j = 0;

	while (true) {
		if (j == i) {
			get_obj(wk, arr)->dat.arr.val = v;
			return;
		}

		assert(get_obj(wk, arr)->dat.arr.have_next);
		arr = get_obj(wk, arr)->dat.arr.next;
		++j;
	}

	assert(false && "unreachable");
}

/*
 * dictionaries
 */

bool
obj_dict_foreach(struct workspace *wk, obj dict, void *ctx, obj_dict_iterator cb)
{
	assert(get_obj(wk, dict)->type == obj_dict);

	if (!get_obj(wk, dict)->dat.dict.len) {
		return true;
	}

	while (true) {
		switch (cb(wk, ctx, get_obj(wk, dict)->dat.dict.key, get_obj(wk, dict)->dat.dict.val)) {
		case ir_cont:
			break;
		case ir_done:
			return true;
		case ir_err:
			return false;
		}

		if (!get_obj(wk, dict)->dat.dict.have_next) {
			break;
		}
		dict = get_obj(wk, dict)->dat.dict.next;
	}

	return true;
}

struct obj_dict_dup_ctx { obj *dict; };

static enum iteration_result
obj_dict_dup_iter(struct workspace *wk, void *_ctx, obj key, obj val)
{
	struct obj_dict_dup_ctx *ctx = _ctx;

	obj_dict_set(wk, *ctx->dict, key, val);

	return ir_cont;
}

bool
obj_dict_dup(struct workspace *wk, obj dict, obj *res)
{
	struct obj_dict_dup_ctx ctx = { .dict = res };

	make_obj(wk, res, obj_dict);

	if (!obj_dict_foreach(wk, dict, &ctx, obj_dict_dup_iter)) {
		return false;
	}

	return true;
}

static enum iteration_result
obj_dict_merge_iter(struct workspace *wk, void *_ctx, obj key, obj val)
{
	obj *res = _ctx;

	obj_dict_set(wk, *res, key, val);

	return ir_cont;
}

bool
obj_dict_merge(struct workspace *wk, obj dict, obj dict2, obj *res)
{
	if (!obj_dict_dup(wk, dict, res)) {
		return false;
	}

	if (!obj_dict_foreach(wk, dict2, res, obj_dict_merge_iter)) {
		return false;
	}

	return true;
}

union obj_dict_key_comparison_key {
	struct {
		const char *s;
		uint32_t len;
	} string;
	uint32_t num;
};

/* other is marked uint32_t since it can be used to represent an obj or a
 * number */
typedef bool ((*obj_dict_key_comparison_func)(struct workspace *wk, union obj_dict_key_comparison_key *key, uint32_t other));

static bool
obj_dict_key_comparison_func_string(struct workspace *wk, union obj_dict_key_comparison_key *key, uint32_t other)
{
	const char *a = wk_objstr(wk, other);
	return strlen(a) == key->string.len
	       && strncmp(a, key->string.s, key->string.len) == 0;
}

static bool
obj_dict_key_comparison_func_objstr(struct workspace *wk, union obj_dict_key_comparison_key *key, uint32_t other)
{
	return strcmp(wk_objstr(wk, key->num), wk_objstr(wk, other)) == 0;
}

static bool
obj_dict_key_comparison_func_int(struct workspace *wk, union obj_dict_key_comparison_key *key, uint32_t other)
{
	return key->num == other;
}

static bool
_obj_dict_index(struct workspace *wk, obj dict,
	union obj_dict_key_comparison_key *key,
	obj_dict_key_comparison_func comp,
	obj **res)

{
	if (!get_obj(wk, dict)->dat.dict.len) {
		return false;
	}

	while (true) {
		if (comp(wk, key, get_obj(wk, dict)->dat.dict.key)) {
			*res = &get_obj(wk, dict)->dat.dict.val;
			return true;
		}

		if (!get_obj(wk, dict)->dat.dict.have_next) {
			break;
		}
		dict = get_obj(wk, dict)->dat.dict.next;
	}

	return false;
}

bool
obj_dict_index_strn(struct workspace *wk, obj dict, const char *str,
	uint32_t len, obj *res)
{
	uint32_t *r;
	union obj_dict_key_comparison_key key = {
		.string = { .s = str, .len = len, }
	};

	if (!_obj_dict_index(wk, dict, &key,
		obj_dict_key_comparison_func_string, &r)) {
		return false;
	}

	*res = *r;

	return true;
}

bool
obj_dict_index(struct workspace *wk, obj dict, obj key, obj *res)
{
	const char *k = wk_objstr(wk, key);
	return obj_dict_index_strn(wk, dict, k, strlen(k), res);
}

bool
obj_dict_in(struct workspace *wk, obj dict, obj key)
{
	obj res;
	return obj_dict_index(wk, dict, key, &res);
}

static void
_obj_dict_set(struct workspace *wk, obj dict,
	obj_dict_key_comparison_func comp, obj key, obj val)
{
	struct obj *d; //, *tail;
	obj tail;

	assert(get_obj(wk, dict)->type == obj_dict);

	/* empty dict */
	if (!(d = get_obj(wk, dict))->dat.dict.len) {
		d->dat.dict.key = key;
		d->dat.dict.val = val;
		d->dat.dict.tail = dict;
		++d->dat.dict.len;
		return;
	}

	obj *r;
	union obj_dict_key_comparison_key k = { .num = key };
	if (_obj_dict_index(wk, dict, &k, comp, &r)) {
		*r = val;
		return;
	}

	/* set new value */
	d = make_obj(wk, &tail, obj_dict);
	d->dat.dict.key = key;
	d->dat.dict.val = val;

	d = get_obj(wk, get_obj(wk, dict)->dat.dict.tail);
	assert(d->type == obj_dict);
	assert(!d->dat.dict.have_next);
	d->dat.dict.have_next = true;
	d->dat.dict.next = tail;

	d = get_obj(wk, dict);

	d->dat.dict.tail = tail;
	++d->dat.dict.len;
}

void
obj_dict_set(struct workspace *wk, obj dict, obj key, obj val)
{
	_obj_dict_set(wk, dict, obj_dict_key_comparison_func_objstr, key, val);
}

/* dict convienence functions */

void
obj_dict_seti(struct workspace *wk, obj dict, uint32_t key, obj val)
{
	_obj_dict_set(wk, dict, obj_dict_key_comparison_func_int, key, val);
}

bool
obj_dict_geti(struct workspace *wk, obj dict, uint32_t key, obj *val)
{
	obj *r;
	if (_obj_dict_index(wk, dict,
		&(union obj_dict_key_comparison_key){ .num = key },
		obj_dict_key_comparison_func_int, &r)) {
		*val = *r;
		return true;
	}

	return false;
}

/* */

struct obj_clone_ctx {
	struct workspace *wk_dest;
	obj container;
};

static enum iteration_result
obj_clone_array_iter(struct workspace *wk_src, void *_ctx, obj val)
{
	struct obj_clone_ctx *ctx = _ctx;

	obj dest_val;

	if (!obj_clone(wk_src, ctx->wk_dest, val, &dest_val)) {
		return ir_err;
	}

	obj_array_push(ctx->wk_dest, ctx->container, dest_val);
	return ir_cont;
}

static enum iteration_result
obj_clone_dict_iter(struct workspace *wk_src, void *_ctx, obj key, obj val)
{
	struct obj_clone_ctx *ctx = _ctx;

	obj dest_key, dest_val;

	if (!obj_clone(wk_src, ctx->wk_dest, key, &dest_key)) {
		return ir_err;
	} else if (!obj_clone(wk_src, ctx->wk_dest, val, &dest_val)) {
		return ir_err;
	}

	obj_dict_set(ctx->wk_dest, ctx->container, dest_key, dest_val);
	return ir_cont;
}

bool
obj_clone(struct workspace *wk_src, struct workspace *wk_dest, obj val, obj *ret)
{
	enum obj_type t = get_obj(wk_src, val)->type;
	struct obj *o;

	/* L("cloning %s", obj_type_to_s(t)); */

	switch (t) {
	case obj_null:
		*ret = 0;
		return true;
	case obj_number:
	case obj_bool: {
		o = make_obj(wk_dest, ret, t);
		*o = *get_obj(wk_src, val);
		return true;
	}
	case obj_string:
		o = make_obj(wk_dest, ret, t);
		o->dat.str = wk_str_push(wk_dest, wk_objstr(wk_src, val));
		return true;
	case obj_file:
		o = make_obj(wk_dest, ret, t);
		o->dat.file = wk_str_push(wk_dest, wk_file_path(wk_src, val));
		return true;
	case obj_array:
		make_obj(wk_dest, ret, t);
		return obj_array_foreach(wk_src, val, &(struct obj_clone_ctx) {
			.container = *ret, .wk_dest = wk_dest
		}, obj_clone_array_iter);
	case obj_dict:
		make_obj(wk_dest, ret, t);
		return obj_dict_foreach(wk_src, val, &(struct obj_clone_ctx) {
			.container = *ret, .wk_dest = wk_dest
		}, obj_clone_dict_iter);
	case obj_test: {
		struct obj *test = get_obj(wk_src, val);

		o = make_obj(wk_dest, ret, t);
		o->dat.test.name = make_str(wk_dest, wk_objstr(wk_src, test->dat.test.name));
		o->dat.test.exe = make_str(wk_dest, wk_objstr(wk_src, test->dat.test.exe));
		o->dat.test.should_fail = test->dat.test.should_fail;

		if (!obj_clone(wk_src, wk_dest, test->dat.test.args, &o->dat.test.args)) {
			return false;
		}

		if (!obj_clone(wk_src, wk_dest, test->dat.test.env, &o->dat.test.env)) {
			return false;
		}

		return true;
	}
	case obj_install_target: {
		struct obj *in = get_obj(wk_src, val);

		o = make_obj(wk_dest, ret, t);
		o->dat.install_target.base_path =
			wk_str_push(wk_dest, wk_str(wk_src, in->dat.install_target.base_path));
		o->dat.install_target.filename =
			wk_str_push(wk_dest, wk_str(wk_src, in->dat.install_target.filename));
		o->dat.install_target.install_dir =
			wk_str_push(wk_dest, wk_str(wk_src, in->dat.install_target.install_dir));

		// TODO
		o->dat.install_target.install_mode = in->dat.install_target.install_mode;
		return true;
	}
	case obj_environment: {
		struct obj *env = get_obj(wk_src, val);
		o = make_obj(wk_dest, ret, obj_environment);

		if (!obj_clone(wk_src, wk_dest, env->dat.environment.env, &o->dat.environment.env)) {
			return false;
		}
		return true;
	}
	default:
		LOG_E("unable to clone '%s'", obj_type_to_s(t));
		return false;
	}
}

struct obj_to_s_ctx {
	char *buf;
	uint32_t i, len;
	uint32_t cont_i, cont_len;
};

static bool _obj_to_s(struct workspace *wk, obj obj, char *buf, uint32_t len, uint32_t *w);

static enum iteration_result
obj_to_s_array_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct obj_to_s_ctx *ctx = _ctx;
	uint32_t w;

	if (!_obj_to_s(wk, val, &ctx->buf[ctx->i], ctx->len - ctx->i, &w)) {
		return ir_err;
	}

	ctx->i += w;

	if (ctx->cont_i < ctx->cont_len - 1) {
		ctx->i += snprintf(&ctx->buf[ctx->i], ctx->len, ", ");
	}

	++ctx->cont_i;
	return ir_cont;
}

static enum iteration_result
obj_to_s_dict_iter(struct workspace *wk, void *_ctx, obj key, obj val)
{
	struct obj_to_s_ctx *ctx = _ctx;
	uint32_t w;

	if (!_obj_to_s(wk, key, &ctx->buf[ctx->i], ctx->len - ctx->i, &w)) {
		return ir_err;
	}
	ctx->i += w;

	ctx->i += snprintf(&ctx->buf[ctx->i], ctx->len, ": ");

	if (!_obj_to_s(wk, val, &ctx->buf[ctx->i], ctx->len - ctx->i, &w)) {
		return ir_err;
	}
	ctx->i += w;

	if (ctx->cont_i < ctx->cont_len - 1) {
		ctx->i += snprintf(&ctx->buf[ctx->i], ctx->len, ", ");
	}

	++ctx->cont_i;
	return ir_cont;
}

static bool
_obj_to_s(struct workspace *wk, obj obj, char *buf, uint32_t len, uint32_t *w)
{
	struct obj_to_s_ctx ctx = { .buf = buf, .len = len };
	enum obj_type t = get_obj(wk, obj)->type;

	switch (t) {
	case obj_build_target: {
		struct obj *tgt = get_obj(wk, obj);
		const char *type = NULL;
		switch (tgt->dat.tgt.type) {
		case tgt_executable:
			type = "executable";
			break;
		case tgt_library:
			type = "library";
			break;
		}

		ctx.i += snprintf(&ctx.buf[ctx.i], len, "<%s %s>", type, wk_str(wk, tgt->dat.tgt.name));
		break;
	}
	case obj_feature_opt:
		switch (get_obj(wk, obj)->dat.feature_opt.state) {
		case feature_opt_auto:
			ctx.i += snprintf(buf, len, "'auto'");
			break;
		case feature_opt_enabled:
			ctx.i += snprintf(buf, len, "'enabled'");
			break;
		case feature_opt_disabled:
			ctx.i += snprintf(buf, len, "'disabled'");
			break;
		}

		break;
	case obj_test: {
		struct obj *test = get_obj(wk, obj);
		ctx.i += snprintf(buf, len, "test('%s', '%s'",
			wk_objstr(wk, test->dat.test.name),
			wk_objstr(wk, test->dat.test.exe)
			);

		if (test->dat.test.args) {
			ctx.i += snprintf(&ctx.buf[ctx.i], len, ", args: ");
			uint32_t w;
			if (!_obj_to_s(wk, test->dat.test.args, &ctx.buf[ctx.i], ctx.len - ctx.i, &w)) {
				return ir_err;
			}
			ctx.i += w;
		}

		if (test->dat.test.should_fail) {
			ctx.i += snprintf(&ctx.buf[ctx.i], len, ", should_fail: true");
		}

		ctx.i += snprintf(&ctx.buf[ctx.i], len, ")");
		break;
	}
	case obj_file:
		ctx.i += snprintf(buf, len, "files('%s')", wk_str(wk, get_obj(wk, obj)->dat.file));
		break;
	case obj_string:
		ctx.i += snprintf(buf, len, "'%s'", wk_objstr(wk, obj));
		break;
	case obj_number:
		ctx.i += snprintf(buf, len, "%ld", (intmax_t)get_obj(wk, obj)->dat.num);
		break;
	case obj_bool:
		if (get_obj(wk, obj)->dat.boolean) {
			ctx.i += snprintf(buf, len, "true");
		} else {
			ctx.i += snprintf(buf, len, "false");
		}
		break;
	case obj_array:
		ctx.cont_len = get_obj(wk, obj)->dat.arr.len;

		ctx.i += snprintf(&ctx.buf[ctx.i], len, "[");
		if (!obj_array_foreach(wk, obj, &ctx, obj_to_s_array_iter)) {
			return false;
		}
		ctx.i += snprintf(&ctx.buf[ctx.i], len, "]");
		break;
	case obj_dict:
		ctx.cont_len = get_obj(wk, obj)->dat.dict.len;

		ctx.i += snprintf(&ctx.buf[ctx.i], len, "{ ");
		if (!obj_dict_foreach(wk, obj, &ctx, obj_to_s_dict_iter)) {
			return false;
		}
		ctx.i += snprintf(&ctx.buf[ctx.i], len, " }");
		break;
	case obj_external_program: {
		struct obj *prog = get_obj(wk, obj);
		ctx.i += snprintf(&ctx.buf[ctx.i], len, "<%s found: %s", obj_type_to_s(t),
			prog->dat.external_program.found ? "true" : "false"
			);

		if (prog->dat.external_program.found) {
			ctx.i += snprintf(&ctx.buf[ctx.i], len, ", path: %s",
				wk_str(wk, prog->dat.external_program.full_path));
		}

		ctx.i += snprintf(&ctx.buf[ctx.i], len, ">");
		break;
	}
	default:
		ctx.i += snprintf(&ctx.buf[ctx.i], len, "<obj %s>", obj_type_to_s(t));
		return true;
	}

	*w = ctx.i;
	return true;
}

bool
obj_to_s(struct workspace *wk, obj obj, char *buf, uint32_t len)
{
	uint32_t w;
	return _obj_to_s(wk, obj, buf, len, &w);
}

bool
obj_vsnprintf(struct workspace *wk, char *out_buf, uint32_t buflen, const char *fmt, va_list ap_orig)
{
#define CHECK_TRUNC(len) if (bufi + len > BUF_SIZE_4k) goto would_truncate

	static char fmt_buf[BUF_SIZE_4k];

	const char *fmt_start, *s;
	uint32_t bufi = 0, len;
	obj obj;
	bool got_object;
	va_list ap, ap_copy;

	va_copy(ap, ap_orig);
	va_copy(ap_copy, ap);

	for (; *fmt; ++fmt) {
		if (*fmt == '%') {
			got_object = false;
			fmt_start = fmt;
			++fmt;

			// skip flags
			while (strchr("#0- +", *fmt)) {
				++fmt;
			}
			// skip field width / precision
			while (strchr("1234567890.", *fmt)) {
				++fmt;
			}
			// skip field length modifier
			while (strchr("hlLjzt", *fmt)) {
				++fmt;
			}

			switch (*fmt) {
			case 'c':
			case 'd': case 'i':
				va_arg(ap, int);
				break;
			case 'u': case 'x': case 'X':
				va_arg(ap, unsigned int);
				break;
			case 'e': case 'E':
			case 'f': case 'F':
			case 'g': case 'G':
			case 'a': case 'A':
				va_arg(ap, double);
				break;
			case 's':
				va_arg(ap, char *);
				break;
			case 'p':
				va_arg(ap, void *);
				break;
			case 'n':
			case '%':
				break;
			case 'o':
				got_object = true;
				obj = va_arg(ap, uint32_t);
				break;
			default:
				assert(false && "unrecognized format");
				break;
			}

			if (got_object) {
				if (!obj_to_s(wk, obj, out_buf, BUF_SIZE_4k)) {
					goto would_truncate;
				}

				// escape % and copy to fmt
				for (s = out_buf; *s; ++s) {
					if (*s == '%') {
						CHECK_TRUNC(1);
						fmt_buf[bufi] = '%';
						++bufi;
					}

					CHECK_TRUNC(1);
					fmt_buf[bufi] = *s;
					++bufi;
				}
			} else {
				len = fmt - fmt_start + 1;
				CHECK_TRUNC(len);
				memcpy(&fmt_buf[bufi], fmt_start, len);
				bufi += len;
			}
		} else {
			CHECK_TRUNC(1);
			fmt_buf[bufi] = *fmt;
			++bufi;
		}
	}

	CHECK_TRUNC(1);
	fmt_buf[bufi] = 0;
	++bufi;

	vsnprintf(out_buf, buflen, fmt_buf, ap_copy);

	va_end(ap);
	va_end(ap_copy);
	return true;
would_truncate:
	va_end(ap);
	va_end(ap_copy);
	return false;

#undef CHECK_TRUNC
}

bool
obj_snprintf(struct workspace *wk, char *out_buf, uint32_t buflen, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	bool ret = obj_vsnprintf(wk, out_buf, buflen, fmt, ap);
	va_end(ap);
	return ret;
}

bool
obj_vfprintf(struct workspace *wk, FILE *f, const char *fmt, va_list ap)
{
	char buf[BUF_SIZE_4k];
	bool ret = obj_vsnprintf(wk, buf, BUF_SIZE_4k, fmt, ap);
	fputs(buf, f);
	return ret;
}

bool
obj_fprintf(struct workspace *wk, FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	bool ret = obj_vfprintf(wk, f, fmt, ap);
	va_end(ap);
	return ret;
}

bool
obj_printf(struct workspace *wk, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	bool ret = obj_vfprintf(wk, stdout, fmt, ap);
	va_end(ap);
	return ret;
}
