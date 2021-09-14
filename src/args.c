#include "posix.h"

#include <assert.h>
#include <string.h>

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

static bool
escape_str(char *buf, uint32_t len, const char *str, char esc_char, const char *need_escaping)
{
	const char *s = str;
	uint32_t bufi = 0;
	bool esc;

	for (; *s; ++s) {
		esc = strchr(need_escaping, *s) != NULL;

		if (bufi + 1 + (esc ? 1 : 0) >= len - 1) {
			LOG_E("truncation during escape");
			return false;
		}

		if (esc) {
			buf[bufi] = esc_char;
			++bufi;
		}

		buf[bufi] = *s;
		++bufi;
	}

	assert(bufi < len);
	buf[bufi] = 0;
	return true;
}

static bool
shell_escape(char *buf, uint32_t len, const char *str)
{
	return escape_str(buf, len, str, '\\', "\"'$ \\><&#");
}

bool
ninja_escape(char *buf, uint32_t len, const char *str)
{
	return escape_str(buf, len, str, '$', " :$");
}

typedef bool ((*escape_func)(char *buf, uint32_t len, const char *str));

struct join_args_iter_ctx {
	uint32_t i, len;
	uint32_t *obj;
	escape_func escape;
};

static enum iteration_result
join_args_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct join_args_iter_ctx *ctx = _ctx;

	assert(get_obj(wk, val)->type == obj_string);

	const char *s = wk_objstr(wk, val);
	char buf[BUF_SIZE_4k];

	if (ctx->escape) {
		if (!ctx->escape(buf, BUF_SIZE_4k, s)) {
			return ir_err;
		}

		s = buf;
	}

	wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, s);

	if (ctx->i < ctx->len - 1) {
		wk_str_app(wk, &get_obj(wk, *ctx->obj)->dat.str, " ");
	}

	++ctx->i;

	return ir_cont;
}

static uint32_t
join_args(struct workspace *wk, uint32_t arr, escape_func escape)
{
	uint32_t obj;
	make_obj(wk, &obj, obj_string)->dat.str = wk_str_push(wk, "");

	struct join_args_iter_ctx ctx = {
		.obj = &obj,
		.len = get_obj(wk, arr)->dat.arr.len,
		.escape = escape
	};

	if (!obj_array_foreach(wk, arr, &ctx, join_args_iter)) {
		assert(false);
		return 0;
	}

	return obj;
}

uint32_t
join_args_plain(struct workspace *wk, uint32_t arr)
{
	return join_args(wk, arr, NULL);
}

uint32_t
join_args_shell(struct workspace *wk, uint32_t arr)
{
	return join_args(wk, arr, shell_escape);
}

uint32_t
join_args_ninja(struct workspace *wk, uint32_t arr)
{
	return join_args(wk, arr, ninja_escape);
}

struct join_args_argv_iter_ctx {
	char **argv;
	uint32_t len;
	uint32_t i;
};

static enum iteration_result
join_args_argv_iter(struct workspace *wk, void *_ctx, uint32_t v)
{
	struct join_args_argv_iter_ctx *ctx = _ctx;

	if (ctx->i >= ctx->len - 1) {
		return ir_err;
	}

	ctx->argv[ctx->i] = wk_objstr(wk, v);
	++ctx->i;
	return ir_cont;
}

bool
join_args_argv(struct workspace *wk, char **argv, uint32_t len, uint32_t arr)
{
	struct join_args_argv_iter_ctx ctx = {
		.argv = argv,
		.len = len,
	};

	if (!obj_array_foreach(wk, arr, &ctx, join_args_argv_iter)) {
		return false;
	}

	assert(ctx.i < ctx.len);
	ctx.argv[ctx.i] = NULL;

	return true;
}

static enum iteration_result
arr_to_args_iter(struct workspace *wk, void *_ctx, uint32_t src)
{
	uint32_t *res = _ctx;
	uint32_t str, str_obj;

	struct obj *o = get_obj(wk, src);

	switch (o->type) {
	case obj_string:
		str = o->dat.str;
		break;
	case obj_file:
		str = o->dat.file;
		break;
	case obj_build_target: {
		char tmp[PATH_MAX];

		if (!path_join(tmp, PATH_MAX, wk_str(wk, o->dat.tgt.build_dir),
			wk_str(wk, o->dat.tgt.build_name))) {
			return ir_err;
		}

		str = wk_str_push(wk, tmp);
		break;
	}
	case obj_custom_target: {
		obj output_arr = get_obj(wk, src)->dat.custom_target.output;
		if (!obj_array_foreach(wk, output_arr, res, arr_to_args_iter)) {
			return ir_err;
		}
		return ir_cont;
	}
	case obj_external_program:
		str = o->dat.external_program.full_path;
		break;
	default:
		LOG_E("cannot convert '%s' to argument", obj_type_to_s(o->type));
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

	return obj_array_foreach_flat(wk, arr, res, arr_to_args_iter);
}

static bool
push_envp(char envp[MAX_ENV][BUF_SIZE_4k], uint32_t *i, const char *k, const char *v)
{
	if (*i >= MAX_ENV) {
		return false;
	}

	if (strlen(k) + strlen(v) + 1 >= BUF_SIZE_4k) {
		return false;
	}

	sprintf(envp[*i], "%s=%s", k, v);
	++(*i);
	return true;
}

bool
build_envp(struct workspace *wk, char *const *ret[], enum build_envp_flags flags)
{
	static char *envp[MAX_ENV + 1],
		    envd[MAX_ENV][BUF_SIZE_4k];
	uint32_t i = 0, j;

	push_envp(envd, &i, "MESON_BUILD_ROOT", wk->build_root);
	push_envp(envd, &i, "MESON_SOURCE_ROOT", wk->source_root);

	if (flags & build_envp_flag_dist_root) {
		assert(false && "TODO");
	}

	if (flags & build_envp_flag_subdir) {
		push_envp(envd, &i, "MESON_SUBDIR", wk_str(wk, current_project(wk)->cwd));
	}

	for (j = 0; j < i; ++j) {
		envp[j] = envd[j];
	}
	assert(j < MAX_ARGS);
	envp[j] = NULL;

	*ret = envp;
	return true;
}
