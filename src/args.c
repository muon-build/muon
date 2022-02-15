#include "posix.h"

#include <assert.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "compilers.h"
#include "functions/build_target.h"
#include "functions/environment.h"
#include "lang/interpreter.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"

void
push_args(struct workspace *wk, obj arr, const struct args *args)
{
	uint32_t i;
	for (i = 0; i < args->len; ++i) {
		obj_array_push(wk, arr, make_str(wk, args->args[i]));
	}
}

void
push_args_null_terminated(struct workspace *wk, obj arr, char *const *argv)
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

bool
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
	return _ninja_escape(buf, len, str, " :$");
}

static bool
shell_ninja_escape(char *buf, uint32_t len, const char *str)
{
	char tmp_buf[BUF_SIZE_4k];
	if (!shell_escape(tmp_buf, BUF_SIZE_4k, str)) {
		return false;
	} else if (!_ninja_escape(buf, len, tmp_buf, "$")) {
		return false;
	}

	return true;
}

typedef bool ((*escape_func)(char *buf, uint32_t len, const char *str));

struct join_args_iter_ctx {
	uint32_t i, len;
	uint32_t *obj;
	escape_func escape;
};

static enum iteration_result
join_args_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct join_args_iter_ctx *ctx = _ctx;

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

static obj
join_args(struct workspace *wk, obj arr, escape_func escape)
{
	obj o = make_str(wk, "");

	struct join_args_iter_ctx ctx = {
		.obj = &o,
		.len = get_obj_array(wk, arr)->len,
		.escape = escape
	};

	if (!obj_array_foreach(wk, arr, &ctx, join_args_iter)) {
		assert(false);
		return 0;
	}

	return o;
}

obj
join_args_plain(struct workspace *wk, obj arr)
{
	return join_args(wk, arr, NULL);
}

obj
join_args_shell(struct workspace *wk, obj arr)
{
	return join_args(wk, arr, shell_escape);
}

obj
join_args_ninja(struct workspace *wk, obj arr)
{
	return join_args(wk, arr, ninja_escape);
}

obj
join_args_shell_ninja(struct workspace *wk, obj arr)
{
	return join_args(wk, arr, shell_ninja_escape);
}


struct join_args_argv_iter_ctx {
	const char **argv;
	uint32_t len;
	uint32_t i;
};

static enum iteration_result
join_args_argv_iter(struct workspace *wk, void *_ctx, obj v)
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
join_args_argv(struct workspace *wk, const char **argv, uint32_t len, obj arr)
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

struct arr_to_args_ctx {
	enum arr_to_args_flags mode;
	obj res;
};

static enum iteration_result
arr_to_args_iter(struct workspace *wk, void *_ctx, obj src)
{
	struct arr_to_args_ctx *ctx = _ctx;
	obj str;

	enum obj_type t = get_obj_type(wk, src);

	switch (t) {
	case obj_string:
		str = src;
		break;
	case obj_file:
		if (ctx->mode & arr_to_args_relativize_paths) {
			char buf[PATH_MAX];
			if (!path_relative_to(buf, PATH_MAX, wk->build_root, get_file_path(wk, src))) {
				return ir_err;
			}
			str = make_str(wk, buf);
			break;
		}
		str = *get_obj_file(wk, src);
		break;
	case obj_alias_target:
		if (!(ctx->mode & arr_to_args_alias_target)) {
			goto type_err;
		}
		str = get_obj_alias_target(wk, src)->name;
		break;
	case obj_build_target: {
		if (!(ctx->mode & arr_to_args_build_target)) {
			goto type_err;
		}

		char name[PATH_MAX];
		tgt_build_path(wk, get_obj_build_target(wk, src), (ctx->mode & arr_to_args_relativize_paths), name);
		str = make_str(wk, name);
		break;
	}
	case obj_custom_target: {
		if (!(ctx->mode & arr_to_args_custom_target)) {
			goto type_err;
		}

		obj output_arr = get_obj_custom_target(wk, src)->output;
		if (!obj_array_foreach(wk, output_arr, ctx, arr_to_args_iter)) {
			return ir_err;
		}
		return ir_cont;
	}
	case obj_external_program:
		if (!(ctx->mode & arr_to_args_external_program)) {
			goto type_err;
		}

		str = get_obj_external_program(wk, src)->full_path;
		break;
	default:
type_err:
		LOG_E("cannot convert '%s' to argument", obj_type_to_s(t));
		return ir_err;
	}

	obj_array_push(wk, ctx->res, str);
	return ir_cont;
}

bool
arr_to_args(struct workspace *wk, enum arr_to_args_flags mode, obj arr, obj *res)
{
	make_obj(wk, res, obj_array);

	struct arr_to_args_ctx ctx = {
		.mode = mode,
		.res = *res
	};

	return obj_array_foreach_flat(wk, arr, &ctx, arr_to_args_iter);
}

struct env_to_envp_ctx {
	char *envp[MAX_ENV + 1];
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

	ctx->envp[ctx->i] = (char *)s;
	++ctx->i;
	return true;
}

static bool
push_envp_key_val(struct workspace *wk, struct env_to_envp_ctx *ctx, const char *k, const char *v)
{
	obj s = make_strf(wk, "%s=%s", k, v);
	return push_envp_str(ctx, get_cstr(wk, s));
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

	if (!push_envp_key_val(wk, ctx, get_cstr(wk, key), get_cstr(wk, val))) {
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

	push_envp_key_val(wk, &ctx, "MESON_BUILD_ROOT", wk->build_root);
	push_envp_key_val(wk, &ctx, "MESON_SOURCE_ROOT", wk->source_root);

	if (flags & env_to_envp_flag_dist_root) {
		assert(false && "TODO");
	}

	if (flags & env_to_envp_flag_subdir) {
		static char subdir[PATH_MAX];
		if (!path_relative_to(subdir, PATH_MAX, wk->source_root, get_cstr(wk, current_project(wk)->cwd))) {
			return false;
		}

		push_envp_key_val(wk, &ctx, "MESON_SUBDIR", subdir);
	}

	if (!val) {
		return true;
	}

	enum obj_type t = get_obj_type(wk, val);
	switch (t) {
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
		return obj_dict_foreach(wk, get_obj_environment(wk, val)->env, &ctx, env_to_envp_dict_iter);
	default:
		interp_error(wk, err_node, "unable to coerce type '%s' into environment", obj_type_to_s(t));
		return false;
	}
}
