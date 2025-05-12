/*
 * SPDX-FileCopyrightText: NRK <nrk@disroot.org>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "external/pkgconfig.h"
#include "functions/compiler.h"
#include "lang/object_iterators.h"
#include "log.h"
#include "options.h"
#include "platform/os.h"
#include "platform/run_cmd.h"

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
	struct pkgconfig_parse_result res = { 0 };
	char *p = ctx->beg;
	char *e = ctx->end;

	for (; p < e && is_whitespace(*p); ++p) {
	}
	if (p == e) {
		return res;
	}

	// parse the type, special casing -I, -L and -l
	if (*p == '-' && p + 1 < e) {
		switch (p[1]) {
		case 'I':
		case 'L':
		case 'l':
			res.type = p[1];
			p += 2;
			// unusual, but may have whitespace between, e.g "-I /dir/"
			for (; p < e && is_whitespace(*p); ++p) {
			}
			break;
		default: res.type = PKG_CONFIG_PARSE_PASS;
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

static bool
pkgconfig_cmd(struct workspace *wk, struct run_cmd_ctx *rctx, obj extra_args)
{
	obj cmd = make_obj(wk, obj_array);

	obj pkgconfig_exe;
	get_option_value(wk, NULL, "env.PKG_CONFIG", &pkgconfig_exe);
	obj_array_extend(wk, cmd, pkgconfig_exe);

	if (extra_args) {
		obj_array_extend(wk, cmd, extra_args);
	}

	const char *argstr;
	uint32_t argc;
	join_args_argstr(wk, &argstr, &argc, cmd);

	bool ok = run_cmd(rctx, argstr, argc, 0, 0);
	if (!ok) {
		LOG_E("failed to run pkg-config: %s", rctx->err_msg);
	} else if (rctx->status != 0) {
		if (rctx->out.len) {
			tstr_trim_trailing_newline(&rctx->out);
			LOG_I("%s", rctx->out.buf);
		}
		if (rctx->err.len) {
			tstr_trim_trailing_newline(&rctx->err);
			LOG_W("%s", rctx->err.buf);
		}
		ok = false;
	}

	return ok;
}

static bool
pkgconfig_exec_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, struct pkgconfig_info *info)
{
	L("pkg-config-exec: looking up %s %s", get_cstr(wk, name), is_static ? "static" : "dynamic");

	{
		obj args = make_obj(wk, obj_array);
		obj_array_push(wk, args, make_str(wk, "--modversion"));
		obj_array_push(wk, args, name);

		struct run_cmd_ctx rctx = { 0 };
		bool ok = pkgconfig_cmd(wk, &rctx, args);
		if (ok) {
			tstr_trim_trailing_newline(&rctx.out);
			cstr_copy(info->version, &TSTR_STR(&rctx.out));
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

	const char *flag_type[] = { "--cflags", "--libs" };
	obj libdirs = make_obj(wk, obj_array);
	for (int i = 0; i < 2; ++i) {
		obj args = make_obj(wk, obj_array);
		obj_array_push(wk, args, make_str(wk, flag_type[i]));
		if (is_static) {
			obj_array_push(wk, args, make_str(wk, "--static"));
		}
		obj_array_push(wk, args, name);

		struct run_cmd_ctx rctx = { 0 };
		bool ok = pkgconfig_cmd(wk, &rctx, args);
		if (!ok) {
			goto cleanup;
		}

		struct pkgconfig_parse_ctx parse_ctx = { rctx.out.buf, rctx.out.buf + rctx.out.len };
		for (;;) {
			struct pkgconfig_parse_result res = pkgconfig_next_opt(&parse_ctx);
			if (res.type == PKG_CONFIG_PARSE_END) {
				break;
			}

			switch (res.type) {
			case 'L': obj_array_push(wk, libdirs, make_str(wk, res.arg)); break;
			case 'I': {
				obj inc = make_obj(wk, obj_include_directory);
				struct obj_include_directory *incp = get_obj_include_directory(wk, inc);
				incp->path = make_str(wk, res.arg);
				incp->is_system = false;
				obj_array_push(wk, info->includes, inc);
				break;
			}
			case 'l': {
				enum find_library_flag flags = is_static ? find_library_flag_prefer_static : 0;
				struct find_library_result find_result
					= find_library(wk, compiler, res.arg, libdirs, flags);
				if (find_result.found) {
					if (find_result.location == find_library_found_location_link_arg) {
						obj_array_push(wk, info->not_found_libs, make_str(wk, res.arg));
					} else {
						obj_array_push(wk, info->libs, find_result.found);
					}
				} else {
					LOG_W("pkg-config-exec: dependency '%s' missing required library '%s'",
						get_cstr(wk, name),
						res.arg);
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

static bool
pkgconfig_exec_get_variable(struct workspace *wk, obj pkg_name, obj var_name, obj defines, obj *res)
{
	obj args = make_obj(wk, obj_array);
	obj_array_push(wk, args, make_str(wk, "--variable"));
	obj_array_push(wk, args, var_name);
	obj_array_push(wk, args, pkg_name);

	if (defines) {
		obj k, v;
		obj_dict_for(wk, defines, k, v) {
			obj_array_push(
				wk, args, make_strf(wk, "--define-variable=%s=%s", get_cstr(wk, k), get_cstr(wk, v)));
		}
	}

	struct run_cmd_ctx rctx = { 0 };
	bool ok = pkgconfig_cmd(wk, &rctx, args);
	if (ok) {
		tstr_trim_trailing_newline(&rctx.out);
		if (!rctx.out.len) {
			ok = false;
		}
		*res = tstr_into_str(wk, &rctx.out);
	}
	run_cmd_ctx_destroy(&rctx);

	return ok;
}

const struct pkgconfig_impl pkgconfig_impl_exec = {
	.lookup = pkgconfig_exec_lookup,
	.get_variable = pkgconfig_exec_get_variable,
};
