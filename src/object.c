#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "interpreter.h"
#include "log.h"
#include "object.h"
#include "parser.h"

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

	case obj_type_count: assert(false); return "uh oh";
	}

	assert(false && "unreachable");
	return NULL;
}

static bool
typecheck_simple_err(struct workspace *wk, uint32_t obj_id, enum obj_type type)
{
	struct obj *obj = get_obj(wk, obj_id);

	if (type != obj_any && obj->type != type) {
		LOG_W(log_interp, "expected type %s, got %s", obj_type_to_s(type), obj_type_to_s(obj->type));
		return false;
	}

	return true;
}

bool
obj_equal(struct workspace *wk, uint32_t l_id, uint32_t r_id)
{
	if (l_id == r_id) {
		return true;
	}

	struct obj *l = get_obj(wk, l_id),
		   *r = get_obj(wk, r_id);

	if (l->type != r->type) {
		return false;
	}

	switch (l->type) {
	case obj_string:
		return strcmp(wk_str(wk, l->dat.str), wk_str(wk, r->dat.str)) == 0;
	case obj_number:
		return l->dat.num == r->dat.num;
	case obj_bool:
		return l->dat.boolean == r->dat.boolean;
	case obj_array:
	case obj_file:
		L(log_interp, "TODO: compare %s", obj_type_to_s(l->type));
		return false;
	default:
		return false;
	}
}

/*
 * arrays
 */


bool
obj_array_foreach(struct workspace *wk, uint32_t arr_id, void *ctx, obj_array_iterator cb)
{
	assert(get_obj(wk, arr_id)->type == obj_array);

	if (!get_obj(wk, arr_id)->dat.arr.len) {
		return true;
	}

	while (true) {
		switch (cb(wk, ctx, get_obj(wk, arr_id)->dat.arr.l)) {
		case ir_cont:
			break;
		case ir_done:
			return true;
		case ir_err:
			return false;
		}

		if (!get_obj(wk, arr_id)->dat.arr.have_r) {
			break;
		}
		arr_id = get_obj(wk, arr_id)->dat.arr.r;
	}

	return true;
}

struct obj_array_foreach_flat_ctx {
	void *usr_ctx;
	obj_array_iterator cb;
};

static enum iteration_result
obj_array_foreach_flat_iter(struct workspace *wk, void *_ctx, uint32_t val)
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
obj_array_foreach_flat(struct workspace *wk, uint32_t arr_id, void *usr_ctx, obj_array_iterator cb)
{
	struct obj_array_foreach_flat_ctx ctx = {
		.usr_ctx = usr_ctx,
		.cb = cb,
	};

	return obj_array_foreach(wk, arr_id, &ctx, obj_array_foreach_flat_iter);
}

void
obj_array_push(struct workspace *wk, uint32_t arr_id, uint32_t child_id)
{
	uint32_t child_arr_id;
	struct obj *arr, *tail, *child_arr;

	if (!(arr = get_obj(wk, arr_id))->dat.arr.len) {
		arr->dat.arr.tail = arr_id;
		arr->dat.arr.len = 1;
		arr->dat.arr.l = child_id;
		return;
	}

	child_arr = make_obj(wk, &child_arr_id, obj_array);
	child_arr->dat.arr.l = child_id;

	arr = get_obj(wk, arr_id);
	assert(arr->type == obj_array);

	tail = get_obj(wk, arr->dat.arr.tail);
	assert(tail->type == obj_array);
	assert(!tail->dat.arr.have_r);
	tail->dat.arr.have_r = true;
	tail->dat.arr.r = child_arr_id;

	arr->dat.arr.tail = child_arr_id;
	++arr->dat.arr.len;
}

struct obj_array_in_iter_ctx {
	uint32_t l_id;
	bool res;
};

static enum iteration_result
obj_array_in_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct obj_array_in_iter_ctx *ctx = _ctx;

	if (obj_equal(wk, ctx->l_id, v_id)) {
		ctx->res = true;
		return ir_done;
	}

	return ir_cont;
}

bool
obj_array_in(struct workspace *wk, uint32_t l_id, uint32_t r_id, bool *res)
{
	struct obj_array_in_iter_ctx ctx = { .l_id = l_id };
	if (!obj_array_foreach(wk, r_id, &ctx, obj_array_in_iter)) {
		return false;
	}

	*res = ctx.res;
	return true;
}

struct obj_array_index_iter_ctx { uint32_t res, i, tgt; };

static enum iteration_result
obj_array_index_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct obj_array_index_iter_ctx *ctx = _ctx;

	if (ctx->i == ctx->tgt) {
		ctx->res = v_id;
		return ir_done;
	}

	++ctx->i;
	return ir_cont;
}

bool
obj_array_index(struct workspace *wk, uint32_t arr_id, int64_t i, uint32_t *res)
{
	struct obj_array_index_iter_ctx ctx = { .tgt = i };

	if (!obj_array_foreach(wk, arr_id, &ctx, obj_array_index_iter)) {
		LOG_W(log_interp, "obj_array_index failed");
		return false;
	}

	*res = ctx.res;
	return true;
}

struct obj_array_dup_ctx { uint32_t *arr; };

static enum iteration_result
obj_array_dup_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct obj_array_dup_ctx *ctx = _ctx;

	obj_array_push(wk, *ctx->arr, v_id);

	return ir_cont;
}

bool
obj_array_dup(struct workspace *wk, uint32_t arr_id, uint32_t *res)
{
	struct obj_array_dup_ctx ctx = { .arr = res };

	make_obj(wk, res, obj_array);

	if (!obj_array_foreach(wk, arr_id, &ctx, obj_array_dup_iter)) {
		return false;
	}

	return true;
}

void
obj_array_extend(struct workspace *wk, uint32_t a_id, uint32_t b_id)
{
	struct obj *a, *b, *tail;

	assert(get_obj(wk, a_id)->type == obj_array && get_obj(wk, b_id)->type == obj_array);

	if (!(b = get_obj(wk, b_id))->dat.arr.len) {
		return;
	}

	if (!(a = get_obj(wk, a_id))->dat.arr.len) {
		*a = *b;
		return;
	}

	tail = get_obj(wk, a->dat.arr.tail);
	assert(tail->type == obj_array);
	assert(!tail->dat.arr.have_r);
	tail->dat.arr.have_r = true;
	tail->dat.arr.r = b_id;

	a->dat.arr.tail = b->dat.arr.tail;
	a->dat.arr.len += b->dat.arr.len;
}

struct obj_array_join_ctx {
	uint32_t join_id, i, len;
	uint32_t *obj;
};

static enum iteration_result
obj_array_join_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct obj_array_join_ctx *ctx = _ctx;

	if (!typecheck_simple_err(wk, val, obj_string)) {
		return ir_err;
	}

	wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, wk_objstr(wk, val));

	if (ctx->i < ctx->len - 1) {
		wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, wk_str(wk, ctx->join_id));
	}

	++ctx->i;

	return ir_cont;
}

bool
obj_array_join(struct workspace *wk, uint32_t a_id, uint32_t join_id, uint32_t *obj)
{
	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, "");

	if (!typecheck_simple_err(wk, join_id, obj_string)) {
		return false;
	}

	struct obj_array_join_ctx ctx = {
		.join_id = get_obj(wk, join_id)->dat.str,
		.obj = obj,
		.len = get_obj(wk, a_id)->dat.arr.len
	};
	return obj_array_foreach(wk, a_id, &ctx, obj_array_join_iter);
}

/*
 * dictionaries
 */

bool
obj_dict_foreach(struct workspace *wk, uint32_t dict_id, void *ctx, obj_dict_iterator cb)
{
	assert(get_obj(wk, dict_id)->type == obj_dict);

	if (!get_obj(wk, dict_id)->dat.dict.len) {
		return true;
	}

	while (true) {
		switch (cb(wk, ctx, get_obj(wk, dict_id)->dat.dict.key, get_obj(wk, dict_id)->dat.dict.l)) {
		case ir_cont:
			break;
		case ir_done:
			return true;
		case ir_err:
			return false;
		}

		if (!get_obj(wk, dict_id)->dat.dict.have_r) {
			break;
		}
		dict_id = get_obj(wk, dict_id)->dat.dict.r;
	}

	return true;
}

struct obj_dict_dup_ctx { uint32_t *dict; };

static enum iteration_result
obj_dict_dup_iter(struct workspace *wk, void *_ctx, uint32_t k_id, uint32_t v_id)
{
	struct obj_dict_dup_ctx *ctx = _ctx;

	obj_dict_set(wk, *ctx->dict, k_id, v_id);

	return ir_cont;
}

bool
obj_dict_dup(struct workspace *wk, uint32_t arr_id, uint32_t *res)
{
	struct obj_dict_dup_ctx ctx = { .dict = res };

	make_obj(wk, res, obj_dict);

	if (!obj_dict_foreach(wk, arr_id, &ctx, obj_dict_dup_iter)) {
		return false;
	}

	return true;
}

struct obj_dict_index_iter_ctx { const char *key; uint32_t *res, len; bool *found; };

static enum iteration_result
obj_dict_index_iter(struct workspace *wk, void *_ctx, uint32_t k_id, uint32_t v_id)
{
	struct obj_dict_index_iter_ctx *ctx = _ctx;

	/* L(log_interp, "%s ?= %s", wk_objstr(wk, ctx->k_id), wk_objstr(wk, k_id)); */
	if (strlen(wk_objstr(wk, k_id)) == ctx->len
	    && strncmp(wk_objstr(wk, k_id), ctx->key, ctx->len) == 0) {
		*ctx->found = true;
		*ctx->res = v_id;
		return ir_done;
	}

	return ir_cont;
}

bool
obj_dict_index_strn(struct workspace *wk, uint32_t dict_id, const char *key,
	uint32_t len, uint32_t *res, bool *found)
{
	struct obj_dict_index_iter_ctx ctx = { .key = key, .len = len, .res = res, .found = found };

	*ctx.found = false;
	return obj_dict_foreach(wk, dict_id, &ctx, obj_dict_index_iter);
}

bool
obj_dict_index(struct workspace *wk, uint32_t dict_id, uint32_t k_id, uint32_t *res, bool *found)
{
	const char *key = wk_objstr(wk, k_id);
	return obj_dict_index_strn(wk, dict_id, key, strlen(key), res, found);
}

bool
obj_dict_in(struct workspace *wk, uint32_t k_id, uint32_t dict_id, bool *res)
{
	uint32_t res_id;
	return obj_dict_index(wk, dict_id, k_id, &res_id, res);
}

void
obj_dict_set(struct workspace *wk, uint32_t dict_id, uint32_t key_id, uint32_t val_id)
{
	struct obj *dict; //, *tail;
	uint32_t tail_id;

	assert(get_obj(wk, dict_id)->type == obj_dict);

	/* empty dict */
	if (!(dict = get_obj(wk, dict_id))->dat.dict.len) {
		dict->dat.dict.key = key_id;
		dict->dat.dict.l = val_id;
		dict->dat.dict.tail = dict_id;
		++dict->dat.dict.len;
		return;
	}

	{ /* find previously set value */
		uint32_t subdict = dict_id;
		while (true) {
			uint32_t k_id = get_obj(wk, subdict)->dat.dict.key;

			if (strcmp(wk_objstr(wk, key_id), wk_objstr(wk, k_id)) == 0) {
				get_obj(wk, subdict)->dat.dict.l = val_id;
				return;
			}

			if (!get_obj(wk, subdict)->dat.dict.have_r) {
				break;
			}
			subdict = get_obj(wk, subdict)->dat.dict.r;
		}
	}

	/* set new value */
	dict = make_obj(wk, &tail_id, obj_dict);
	dict->dat.dict.key = key_id;
	dict->dat.dict.l = val_id;

	dict = get_obj(wk, get_obj(wk, dict_id)->dat.dict.tail);
	assert(dict->type == obj_dict);
	assert(!dict->dat.dict.have_r);
	dict->dat.dict.have_r = true;
	dict->dat.dict.r = tail_id;

	dict = get_obj(wk, dict_id);

	dict->dat.dict.tail = tail_id;
	++dict->dat.dict.len;
}

struct obj_clone_ctx {
	struct workspace *wk_dest;
	uint32_t container;
};

static enum iteration_result
obj_clone_array_iter(struct workspace *wk_src, void *_ctx, uint32_t val)
{
	struct obj_clone_ctx *ctx = _ctx;

	uint32_t dest_val;

	if (!obj_clone(wk_src, ctx->wk_dest, val, &dest_val)) {
		return ir_err;
	}

	obj_array_push(ctx->wk_dest, ctx->container, dest_val);
	return ir_cont;
}

static enum iteration_result
obj_clone_dict_iter(struct workspace *wk_src, void *_ctx, uint32_t key, uint32_t val)
{
	struct obj_clone_ctx *ctx = _ctx;

	uint32_t dest_key, dest_val;

	if (!obj_clone(wk_src, ctx->wk_dest, key, &dest_key)) {
		return ir_err;
	} else if (!obj_clone(wk_src, ctx->wk_dest, val, &dest_val)) {
		return ir_err;
	}

	obj_dict_set(ctx->wk_dest, ctx->container, dest_key, dest_val);
	return ir_cont;
}

bool
obj_clone(struct workspace *wk_src, struct workspace *wk_dest, uint32_t val, uint32_t *ret)
{
	enum obj_type t = get_obj(wk_src, val)->type;
	struct obj *obj;

	switch (t) {
	case obj_number:
	case obj_bool: {
		obj = make_obj(wk_dest, ret, t);
		*obj = *get_obj(wk_src, val);
		return true;
	}
	case obj_string:
		obj = make_obj(wk_dest, ret, t);
		obj->dat.str = wk_str_push(wk_dest, wk_objstr(wk_src, val));
		return true;
	case obj_file:
		obj = make_obj(wk_dest, ret, t);
		obj->dat.file = wk_str_push(wk_dest, wk_file_path(wk_src, val));
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
	default:
		LOG_W(log_interp, "unable to clone '%s'", obj_type_to_s(t));
		return false;
	}
}

struct obj_to_s_ctx {
	char *buf;
	uint32_t i, len;
	uint32_t cont_i, cont_len;
};

static bool _obj_to_s(struct workspace *wk, uint32_t id, char *buf, uint32_t len, uint32_t *w);

static enum iteration_result
obj_to_s_array_iter(struct workspace *wk, void *_ctx, uint32_t val)
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
obj_to_s_dict_iter(struct workspace *wk, void *_ctx, uint32_t key, uint32_t val)
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
_obj_to_s(struct workspace *wk, uint32_t id, char *buf, uint32_t len, uint32_t *w)
{
	struct obj_to_s_ctx ctx = { .buf = buf, .len = len };
	enum obj_type t = get_obj(wk, id)->type;

	switch (t) {
	case obj_feature_opt:
		switch (get_obj(wk, id)->dat.feature_opt.state) {
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
	case obj_file:
		ctx.i += snprintf(buf, len, "files('%s')", wk_objstr(wk, id));
		break;
	case obj_string:
		ctx.i += snprintf(buf, len, "'%s'", wk_objstr(wk, id));
		break;
	case obj_number:
		ctx.i += snprintf(buf, len, "%ld", (intmax_t)get_obj(wk, id)->dat.num);
		break;
	case obj_bool:
		if (get_obj(wk, id)->dat.boolean) {
			ctx.i += snprintf(buf, len, "true");
		} else {
			ctx.i += snprintf(buf, len, "false");
		}
		break;
	case obj_array:
		ctx.cont_len = get_obj(wk, id)->dat.arr.len;

		ctx.i += snprintf(&ctx.buf[ctx.i], len, "[");
		if (!obj_array_foreach(wk, id, &ctx, obj_to_s_array_iter)) {
			return false;
		}
		ctx.i += snprintf(&ctx.buf[ctx.i], len, "]");
		break;
	case obj_dict:
		ctx.cont_len = get_obj(wk, id)->dat.dict.len;

		ctx.i += snprintf(&ctx.buf[ctx.i], len, "{ ");
		if (!obj_dict_foreach(wk, id, &ctx, obj_to_s_dict_iter)) {
			return false;
		}
		ctx.i += snprintf(&ctx.buf[ctx.i], len, " }");
		break;
	default:
		LOG_W(log_interp, "unable to convert '%s' to string", obj_type_to_s(t));
		return false;
	}

	*w = ctx.i;
	return true;
}

bool
obj_to_s(struct workspace *wk, uint32_t id, char *buf, uint32_t len)
{
	uint32_t w;
	return _obj_to_s(wk, id, buf, len, &w);
}
