/*
 * SPDX-FileCopyrightText: NRK <nrk@disroot.org>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "log.h"
#include "options.h"
#include "platform/os.h"
#include "platform/run_cmd.h"
#include "functions/compiler.h"
#include "external/pkgconfig.h"

const bool have_libpkgconf = false;
const bool have_pkgconfig_exec = true;

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
pkgconfig_cmd(struct run_cmd_ctx *rctx, char *const *argv)
{
	bool ok = run_cmd_argv(rctx, argv, 0, 0);
	if (!ok) {
		LOG_E("failed to run pkg-config: %s", rctx->err_msg);
	} else if (rctx->err.len) {
		LOG_W("%s", pkgconfig_kill_newline(rctx->err.buf));
		ok = false;
	}
	return ok;
}

static char *
pkgconfig_exe(struct workspace *wk)
{
	obj pkgconfig_exe;
	get_option_value(wk, NULL, "env.PKG_CONFIG", &pkgconfig_exe);
	return (char *)get_cstr(wk, pkgconfig_exe);
}

bool
muon_pkgconf_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, struct pkgconf_info *info)
{
	L("pkg-config-exec: looking up %s %s", get_cstr(wk, name), is_static ? "static" : "dynamic");

	{
		char *argv[] = {
			pkgconfig_exe(wk), "--modversion",
			(char *)get_cstr(wk, name), NULL
		};
		struct run_cmd_ctx rctx = {0};
		bool ok = pkgconfig_cmd(&rctx, argv);
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
		char *argv[] = {
			pkgconfig_exe(wk), flag_type[i],
			NULL, NULL, NULL
		};
		if (is_static) {
			argv[2] = "--static";
			argv[3] = (char *)get_cstr(wk, name);
		} else {
			argv[2] = (char *)get_cstr(wk, name);
		}

		struct run_cmd_ctx rctx = {0};
		bool ok = pkgconfig_cmd(&rctx, argv);
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
	char *argv[] = {
		pkgconfig_exe(wk), "--variable",
		(char *)var, (char *)pkg_name, 0
	};
	struct run_cmd_ctx rctx = {0};
	bool ok = pkgconfig_cmd(&rctx, argv);
	if (ok) {
		*res = make_str(wk, pkgconfig_kill_newline(rctx.out.buf));
	}
	run_cmd_ctx_destroy(&rctx);
	return ok;
}

bool
muon_pkgconf_define(struct workspace *wk, const char *key, const char *value)
{
	// TODO: implement it
	vm_error(wk, "pkg-config-exec: define variable not implemented");
	return false;
}
