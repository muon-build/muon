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
#include "functions/kernel.h"
#include "lang/object_iterators.h"
#include "log.h"
#include "options.h"
#include "platform/os.h"
#include "platform/run_cmd.h"

static bool
pkgconfig_cmd(struct workspace *wk, struct run_cmd_ctx *rctx, obj extra_args)
{
	obj cmd = make_obj(wk, obj_array);

	obj pkgconfig_cmd_arr = 0;

	{
		obj prog;
		struct find_program_ctx ctx = {
			.res = &prog,
			.requirement = requirement_auto,
			.machine = machine_kind_host,
		};
		if (!find_program_check_override(wk, &ctx, make_str(wk, "pkg-config"))) {
			return false;
		}

		if (ctx.found) {
			pkgconfig_cmd_arr = get_obj_external_program(wk, prog)->cmd_array;
		}
	}

	if (!pkgconfig_cmd_arr) {
		get_option_value(wk, NULL, "env.PKG_CONFIG", &pkgconfig_cmd_arr);
	}

	obj_array_extend(wk, cmd, pkgconfig_cmd_arr);

	if (extra_args) {
		obj_array_extend(wk, cmd, extra_args);
	}

	const char *argstr;
	uint32_t argc;
	join_args_argstr(wk, &argstr, &argc, cmd);

	return run_cmd_checked(rctx, argstr, argc, 0, 0);
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
	obj libdirs = make_obj(wk, obj_array);

	enum flag_type {
		flag_type_cflags,
		flag_type_libs,
		flag_type_count,
	} flag_type;
	const char *flag_type_str[] = { "--cflags", "--libs" };

	for (flag_type = 0; flag_type < flag_type_count; ++flag_type) {
		obj args = make_obj(wk, obj_array);
		obj_array_push(wk, args, make_str(wk, flag_type_str[flag_type]));
		if (is_static) {
			obj_array_push(wk, args, make_str(wk, "--static"));
		}
		obj_array_push(wk, args, name);

		struct run_cmd_ctx rctx = { 0 };
		bool ok = pkgconfig_cmd(wk, &rctx, args);
		if (!ok) {
			goto cleanup;
		}

		char type = 0;
		obj arg, split = str_shell_split(wk, &TSTR_STR(&rctx.out), shell_type_posix);
		obj_array_for(wk, split, arg) {
			if (!type) {
				const struct str *arg_str = get_str(wk, arg);
				if (arg_str->s[0] == '-' && arg_str->len > 1) {
					if (strchr("LIl", arg_str->s[1])) {
						type = arg_str->s[1];
						if (arg_str->len > 2) {
							arg = make_strn(wk, arg_str->s + 2, arg_str->len - 2);
						} else {
							continue;
						}
					}
				}
			}

			switch (type) {
			case 'L': obj_array_push(wk, libdirs, arg); break;
			case 'I': {
				obj inc = make_obj(wk, obj_include_directory);
				struct obj_include_directory *incp = get_obj_include_directory(wk, inc);
				incp->path = arg;
				incp->is_system = false;
				obj_array_push(wk, info->includes, inc);
				break;
			}
			case 'l': {
				enum find_library_flag flags = is_static ? find_library_flag_prefer_static : 0;
				struct find_library_result find_result
					= find_library(wk, compiler, get_str(wk, arg)->s, libdirs, flags);
				if (find_result.found) {
					if (find_result.location == find_library_found_location_link_arg) {
						obj_array_push(wk, info->not_found_libs, arg);
					} else {
						obj_array_push(wk, info->libs, find_result.found);
					}
				} else {
					LOG_W("pkg-config-exec: dependency '%s' missing required library '%s'",
						get_cstr(wk, name),
						get_cstr(wk, arg));
					obj_array_push(wk, info->not_found_libs, arg);
				}
				break;
			}
			default: {
				obj push = (flag_type == flag_type_cflags) ? info->compile_args : info->link_args;
				obj_array_push(wk, push, arg);
				break;
			}
			}

			type = 0;
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
