#include "posix.h"

#include <assert.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "compilers.h"
#include "functions/environment.h"
#include "lang/interpreter.h"
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
push_args_null_terminated(struct workspace *wk, uint32_t arr, char *const *argv)
{
	char *const *arg;
	for (arg = argv; *arg; ++arg) {
		obj_array_push(wk, arr, make_str(wk, *arg));
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
shell_escape(char *buf, uint32_t len, const char *str)
{
	const char *need_escaping = "\"'$ \\><&#\n";
	const char *s;
	uint32_t bufi = 0;
	bool do_esc = false;

	for (s = str; *s; ++s) {
		if (strchr(need_escaping, *s)) {
			do_esc = true;
			break;
		}
	}

	if (!do_esc) {
		if (len <= strlen(str)) {
			goto trunc;
		}

		strncpy(buf, str, len);
		return true;
	}

	if (!buf_push(buf, len, &bufi, '\'')) {
		goto trunc;
	}

	for (s = str; *s; ++s) {
		if (*s == '\'') {
			if (!buf_pushs(buf, len, &bufi, "'\\''")) {
				goto trunc;
			}
		} else {
			if (!buf_push(buf, len, &bufi, *s)) {
				goto trunc;
			}
		}
	}

	if (!buf_push(buf, len, &bufi, '\'')) {
		goto trunc;
	} else if (!buf_push(buf, len, &bufi, 0)) {
		goto trunc;
	}

	return true;
trunc:
	LOG_E("shell escape truncated");
	return false;
}

static bool
_ninja_escape(char *buf, uint32_t len, const char *str, const char *need_escaping)
{
	char esc_char = '$';
	const char *s = str;
	uint32_t bufi = 0;

	for (; *s; ++s) {
		if (*s == '\n') {
			assert(false && "newlines cannot be escaped" );
		}

		if (strchr(need_escaping, *s)) {
			if (!buf_push(buf, len, &bufi, esc_char)) {
				goto trunc;
			}
		}

		if (!buf_push(buf, len, &bufi, *s)) {
			goto trunc;
		}
	}

	if (!buf_push(buf, len, &bufi, 0)) {
		goto trunc;
	}
	return true;
trunc:
	LOG_E("ninja escape truncated");
	return false;
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

	const char *s = get_cstr(wk, val);
	char buf[BUF_SIZE_4k];

	if (ctx->escape) {
		if (!ctx->escape(buf, BUF_SIZE_4k, s)) {
			return ir_err;
		}

		s = buf;
	}

	str_app(wk, *ctx->obj, s);

	if (ctx->i < ctx->len - 1) {
		str_app(wk, *ctx->obj, " ");
	}

	++ctx->i;

	return ir_cont;
}

static uint32_t
join_args(struct workspace *wk, uint32_t arr, escape_func escape)
{
	obj o = make_str(wk, "");

	struct join_args_iter_ctx ctx = {
		.obj = &o,
		.len = get_obj(wk, arr)->dat.arr.len,
		.escape = escape
	};

	if (!obj_array_foreach(wk, arr, &ctx, join_args_iter)) {
		assert(false);
		return 0;
	}

	return o;
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
	const char **argv;
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

	ctx->argv[ctx->i] = get_cstr(wk, v);
	++ctx->i;
	return ir_cont;
}

bool
join_args_argv(struct workspace *wk, const char **argv, uint32_t len, uint32_t arr)
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
	obj *res = _ctx;
	obj str;

	struct obj *o = get_obj(wk, src);

	switch (o->type) {
	case obj_string:
		str = src;
		break;
	case obj_file:
		str = o->dat.file;
		break;
	case obj_build_target: {
		char tmp[PATH_MAX];

		if (!path_join(tmp, PATH_MAX, get_cstr(wk, o->dat.tgt.build_dir),
			get_cstr(wk, o->dat.tgt.build_name))) {
			return ir_err;
		}

		str = make_str(wk, tmp);
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

	obj_array_push(wk, *res, str);
	return ir_cont;
}

bool
arr_to_args(struct workspace *wk, uint32_t arr, uint32_t *res)
{
	make_obj(wk, res, obj_array);

	return obj_array_foreach_flat(wk, arr, res, arr_to_args_iter);
}

struct env_to_envp_ctx {
	char *envp[MAX_ENV + 1], envd[MAX_ENV][BUF_SIZE_4k];
	uint32_t i;
};

static bool
push_envp_str(struct env_to_envp_ctx *ctx, const char *s)
{
	if (ctx->i >= MAX_ENV) {
		LOG_E("too many environment elements (max: %d)", MAX_ENV);
		return false;
	}

	if (!strchr(s, '=')) {
		LOG_E("env elements must be of the format key=value: '%s'", s);
		return false;
	}

	if (strlen(s) + 1 >= BUF_SIZE_4k) {
		LOG_E("env element too long: '%s'", s);
		return false;
	}

	strncpy(ctx->envd[ctx->i], s, BUF_SIZE_4k);
	ctx->envp[ctx->i] = ctx->envd[ctx->i];
	++ctx->i;
	return true;
}

static bool
push_envp_key_val(struct env_to_envp_ctx *ctx, const char *k, const char *v)
{
	if (strlen(k) + strlen(v) + 1 >= BUF_SIZE_4k) {
		LOG_E("env element too long: '%s=%s'", k, v);
		return false;
	}

	char buf[BUF_SIZE_4k];
	sprintf(buf, "%s=%s", k, v);
	return push_envp_str(ctx, buf);
}

static enum iteration_result
env_to_envp_arr_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct env_to_envp_ctx *ctx = _ctx;

	if (!push_envp_str(ctx, get_cstr(wk, val))) {
		return ir_err;
	}

	return ir_cont;
}

static enum iteration_result
env_to_envp_dict_iter(struct workspace *wk, void *_ctx, obj key, obj val)
{
	struct env_to_envp_ctx *ctx = _ctx;

	if (!push_envp_key_val(ctx, get_cstr(wk, key), get_cstr(wk, val))) {
		return ir_err;
	}

	return ir_cont;
}

bool
env_to_envp(struct workspace *wk, uint32_t err_node, char *const *ret[], obj val, enum env_to_envp_flags flags)
{
	static struct env_to_envp_ctx ctx = { 0 };
	memset(&ctx, 0, sizeof(struct env_to_envp_ctx));

	*ret = ctx.envp;

	push_envp_key_val(&ctx, "MESON_BUILD_ROOT", wk->build_root);
	push_envp_key_val(&ctx, "MESON_SOURCE_ROOT", wk->source_root);

	if (flags & env_to_envp_flag_dist_root) {
		assert(false && "TODO");
	}

	if (flags & env_to_envp_flag_subdir) {
		push_envp_key_val(&ctx, "MESON_SUBDIR", get_cstr(wk, current_project(wk)->cwd));
	}

	if (!val) {
		return true;
	}

	struct obj *v = get_obj(wk, val);
	switch (v->type) {
	case obj_string:
		return push_envp_str(&ctx, get_cstr(wk, val));
	case obj_array:
		return obj_array_foreach_flat(wk, val, &ctx, env_to_envp_arr_iter);
	case obj_dict:
		if (!typecheck_environment_dict(wk, err_node, val)) {
			return false;
		}
		return obj_dict_foreach(wk, val, &ctx, env_to_envp_dict_iter);
	case obj_environment:
		return obj_dict_foreach(wk, v->dat.environment.env, &ctx, env_to_envp_dict_iter);
	default:
		interp_error(wk, err_node, "unable to coerce type '%s' into environment", obj_type_to_s(v->type));
		return false;
	}
}
