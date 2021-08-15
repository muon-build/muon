#include "posix.h"

#include <assert.h>

#include "args.h"
#include "buf_size.h"
#include "compilers.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"

void
push_args(struct workspace *wk, uint32_t arr, const struct args *args)
{
	uint32_t i;
	for (i = 0; i < args->len; ++i) {
		obj_array_push(wk, arr, make_str(wk, args->args[i]));
	}
}

void
push_argv_single(const char **argv, uint32_t *len, uint32_t max, const char *arg)
{
	assert(*len < max && "too many arguments");
	argv[*len] = arg;
	++(*len);
}

void
push_argv(const char **argv, uint32_t *len, uint32_t max, const struct args *args)
{
	uint32_t i;
	for (i = 0; i < args->len; ++i) {
		push_argv_single(argv, len, max, args->args[i]);
	}
}

struct join_args_iter_ctx {
	uint32_t i, len;
	uint32_t *obj;
};

static enum iteration_result
join_args_iter_shell(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct join_args_iter_ctx *ctx = _ctx;

	assert(get_obj(wk, val)->type == obj_string);

	const char *s = wk_objstr(wk, val);
	bool needs_escaping = false;

	for (; *s; ++s) {
		if (*s == '"' || *s == ' ') {
			needs_escaping = true;
		}
	}

	if (needs_escaping) {
		wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, "'");
	}

	wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, wk_objstr(wk, val));

	if (needs_escaping) {
		wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, "'");
	}

	if (ctx->i < ctx->len - 1) {
		wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, " ");
	}

	++ctx->i;

	return ir_cont;
}

static enum iteration_result
join_args_iter_ninja(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct join_args_iter_ctx *ctx = _ctx;

	assert(get_obj(wk, val)->type == obj_string);

	const char *s = wk_objstr(wk, val);

	char buf[BUF_SIZE_2k] = { 0 };
	uint32_t bufi = 0, check;

	for (; *s; ++s) {
		if (*s == ' ' || *s == ':' || *s == '$') {
			check = 2;
		} else {
			check = 1;
		}

		if (bufi + check >= BUF_SIZE_2k) {
			LOG_E("ninja argument too long");
			return ir_err;
		}

		if (check == 2) {
			buf[bufi] = '$';
			++bufi;
		}

		buf[bufi] = *s;
		++bufi;
	}

	wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, buf);

	if (ctx->i < ctx->len - 1) {
		wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, " ");
	}

	++ctx->i;

	return ir_cont;
}

static uint32_t
join_args(struct workspace *wk, uint32_t arr, obj_array_iterator cb)
{
	uint32_t obj;
	make_obj(wk, &obj, obj_string)->dat.str = wk_str_push(wk, "");

	struct join_args_iter_ctx ctx = {
		.obj = &obj,
		.len = get_obj(wk, arr)->dat.arr.len
	};

	if (!obj_array_foreach(wk, arr, &ctx, cb)) {
		assert(false);
		return 0;
	}

	return obj;
}

uint32_t
join_args_shell(struct workspace *wk, uint32_t arr)
{
	return join_args(wk, arr, join_args_iter_shell);
}

const char **
join_args_shell_argv(struct workspace *wk, uint32_t arr)
{
	// TODO
	return NULL;
}

uint32_t
join_args_ninja(struct workspace *wk, uint32_t arr)
{
	return join_args(wk, arr, join_args_iter_ninja);
}

static enum iteration_result
arr_to_args_iter(struct workspace *wk, void *_ctx, uint32_t src)
{
	uint32_t *res = _ctx;
	uint32_t str, str_obj;

	struct obj *obj = get_obj(wk, src);

	switch (obj->type) {
	case obj_string:
		str = obj->dat.str;
		break;
	case obj_file:
		str = obj->dat.file;
		break;
	case obj_build_target: {
		char tmp[PATH_MAX], path[PATH_MAX];

		if (!path_join(tmp, PATH_MAX, wk_str(wk, obj->dat.tgt.build_dir),
			wk_str(wk, obj->dat.tgt.build_name))) {
			return ir_err;
		}

		str = wk_str_push(wk, path);
		break;
	}
	default:
		LOG_E("cannot convert '%s' to argument", obj_type_to_s(obj->type));
		return ir_err;
	}

	make_obj(wk, &str_obj, obj_string)->dat.str = str;
	obj_array_push(wk, *res, str_obj);
	return ir_cont;
}

bool
arr_to_args(struct workspace *wk, uint32_t arr, uint32_t *res)
{
	make_obj(wk, res, obj_array);

	return obj_array_foreach(wk, arr, res, arr_to_args_iter);
}
