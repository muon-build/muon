/*
 * SPDX-FileCopyrightText: NRK <nrk@disroot.org>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "log.h"
#include "buf_size.h"
#include "options.h"
#include "platform/os.h"
#include "platform/mem.h"
#include "platform/run_cmd.h"
#include "functions/compiler.h"
#include "external/pkgconfig.h"

const bool have_libpkgconf = false;
const bool have_pkgconfig_exec = true;

static struct {
	obj defines;
	size_t defines_len;
} pkgconfig_ctx = {0};

struct pkgconfig_parse_ctx {
	char *beg, *end;
};
enum pkgconfig_parse_type {
	PKG_CONFIG_PARSE_END = 0,
	PKG_CONFIG_PARSE_PASS,
};
struct pkgconfig_parse_result {
	char type;
	char *arg;
};

struct pkgconfig_parse_result
pkgconfig_next_opt(struct pkgconfig_parse_ctx *ctx)
{
	struct pkgconfig_parse_result res = {0};
	char *p = ctx->beg;
	char *e = ctx->end;

	for (; p < e && is_whitespace(*p); ++p) {}
	if (p == e) {
		return res;
	}

	// parse the type, special casing -I, -L and -l
	if (*p == '-' && p+1 < e) {
		switch (p[1]) {
		case 'I': case 'L': case 'l':
			res.type = p[1];
			p += 2;
			// unusual, but may have whitespace between, e.g "-I /dir/"
			for (; p < e && is_whitespace(*p); ++p) {}
			break;
		default:
			res.type = PKG_CONFIG_PARSE_PASS;
		}
	} else {
		res.type = PKG_CONFIG_PARSE_PASS;
	}

	// do some very basic shell-like backslash processing.
	// the useful/practical part is "\ " -> " " conversion so that paths
	// containing spaces (e.g on windows) will work properly.
	char *w = p;
	res.arg = w;
	while (p < e) {
		if (is_whitespace(*p)) {
			*w = '\0';
			p += 1;
			break;
		} else if (*p == '\\' && p + 1 < e) {
			switch (p[1]) {
			case '\n': /* nop */ break;
			default: *w++ = p[1]; break;
			}
			p += 2;
		} else {
			*w++ = *p++;
		}
	}
	ctx->beg = p;
	return res;
}

static char *
pkgconfig_kill_newline(char *s)
{
	char *e = strrchr(s, '\n');
	if (e) {
		*e = 0;
	}
	return s;
}

static bool
pkgconfig_cmd(struct workspace *wk, struct run_cmd_ctx *rctx,
	char *const *args, size_t args_len)
{
	size_t len = 0;
	size_t argv_size = 1 + pkgconfig_ctx.defines_len + args_len + 1; // +1 exe, +1 NULL
	char **argv = z_malloc(argv_size * sizeof *argv);

	obj pkgconfig_exe;
	get_option_value(wk, NULL, "env.PKG_CONFIG", &pkgconfig_exe);
	argv[len++] = (char *)get_cstr(wk, pkgconfig_exe);
	for (size_t i = 0; i < pkgconfig_ctx.defines_len; ++i) {
		obj s = obj_array_index(wk, pkgconfig_ctx.defines, i);
		argv[len++] = (char *)get_cstr(wk, s);
	}
	for (size_t i = 0; i < args_len; ++i) {
		argv[len++] = args[i];
	}
	argv[len++] = NULL;

	bool ok = run_cmd_argv(rctx, argv, 0, 0);
	if (!ok) {
		LOG_E("failed to run pkg-config: %s", rctx->err_msg);
	} else if (rctx->err.len) {
		LOG_W("%s", pkgconfig_kill_newline(rctx->err.buf));
		ok = false;
	}
	z_free(argv);
	return ok;
}

bool
muon_pkgconf_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, struct pkgconf_info *info)
{
	L("pkg-config-exec: looking up %s %s", get_cstr(wk, name), is_static ? "static" : "dynamic");

	{
		char *args[] = { "--modversion", (char *)get_cstr(wk, name) };
		struct run_cmd_ctx rctx = {0};
		bool ok = pkgconfig_cmd(wk, &rctx, args, ARRAY_LEN(args));
		if (ok) {
			strncpy(info->version, pkgconfig_kill_newline(rctx.out.buf), MAX_VERSION_LEN);
		}
		run_cmd_ctx_destroy(&rctx);
		if (!ok) {
			return false;
		}
	}

	info->compile_args = make_obj(wk, obj_array);
	info->link_args = make_obj(wk, obj_array);
	info->includes = make_obj(wk, obj_array);
	info->libs = make_obj(wk, obj_array);
	info->not_found_libs = make_obj(wk, obj_array);

	char *flag_type[] = { "--cflags", "--libs" };
	obj libdirs = make_obj(wk, obj_array);
	for (int i = 0; i < 2; ++i) {
		size_t args_len = 0;
		char *args[3] = {0};
		args[args_len++] = flag_type[i];
		if (is_static) {
			args[args_len++] = "--static";
		}
		args[args_len++] = (char *)get_cstr(wk, name);

		struct run_cmd_ctx rctx = {0};
		bool ok = pkgconfig_cmd(wk, &rctx, args, args_len);
		if (!ok) {
			goto cleanup;
		}

		struct pkgconfig_parse_ctx parse_ctx = {
			rctx.out.buf, rctx.out.buf + rctx.out.len
		};
		for (;;) {
			struct pkgconfig_parse_result res = pkgconfig_next_opt(&parse_ctx);
			if (res.type == PKG_CONFIG_PARSE_END) {
				break;
			}

			switch (res.type) {
			case 'L':
				obj_array_push(wk, libdirs, make_str(wk, res.arg));
				break;
			case 'I': {
				obj inc = make_obj(wk, obj_include_directory);
				struct obj_include_directory *incp =
					get_obj_include_directory(wk, inc);
				incp->path = make_str(wk, res.arg);
				incp->is_system = false;
				obj_array_push(wk, info->includes, inc);
				break;
			}
			case 'l': {
				enum find_library_flag flags = is_static ? find_library_flag_prefer_static : 0;
				struct find_library_result find_result =
					find_library(wk, compiler, res.arg, libdirs, flags);
				if (find_result.found) {
					if (find_result.location == find_library_found_location_link_arg) {
						obj_array_push(wk, info->not_found_libs, make_str(wk, res.arg));
					} else {
						obj_array_push(wk, info->libs, find_result.found);
					}
				} else {
					LOG_W("pkg-config-exec: dependency '%s' missing required library '%s'",
						get_cstr(wk, name), res.arg);
					obj_array_push(wk, info->not_found_libs, make_str(wk, res.arg));
				}
				break;
			}
			default: {
				obj push = (i == 0) ? info->compile_args : info->link_args;
				obj_array_push(wk, push, make_str(wk, res.arg));
				break;
			}
			}
		}
cleanup:
		run_cmd_ctx_destroy(&rctx);
		if (!ok) {
			return false;
		}
	}

	return true;
}

bool
muon_pkgconf_get_variable(struct workspace *wk, const char *pkg_name, const char *var, obj *res)
{
	char *args[] = { "--variable", (char *)var, (char *)pkg_name };
	struct run_cmd_ctx rctx = {0};
	bool ok = pkgconfig_cmd(wk, &rctx, args, ARRAY_LEN(args));
	if (ok) {
		*res = make_str(wk, pkgconfig_kill_newline(rctx.out.buf));
	}
	run_cmd_ctx_destroy(&rctx);
	return ok;
}

bool
muon_pkgconf_define(struct workspace *wk, const char *key, const char *value)
{
	if (pkgconfig_ctx.defines == 0) {
		pkgconfig_ctx.defines = make_obj(wk, obj_array);
	}
	struct tstr arg = {0};
	tstr_pushs(wk, &arg, "--define-variable=");
	tstr_pushs(wk, &arg, key);
	tstr_pushs(wk, &arg, "=");
	tstr_pushs(wk, &arg, value);
	obj_array_push(wk, pkgconfig_ctx.defines, make_str(wk, arg.buf));
	++pkgconfig_ctx.defines_len;
	return true;
}
