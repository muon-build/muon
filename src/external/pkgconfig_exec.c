/*
 * SPDX-FileCopyrightText: NRK <nrk@disroot.org>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "external/pkgconfig.h"
#include "functions/environment.h"
#include "functions/kernel.h"
#include "lang/object_iterators.h"
#include "log.h"
#include "options.h"
#include "platform/os.h"
#include "platform/run_cmd.h"

static bool
pkgconfig_cmd(struct workspace *wk, struct run_cmd_ctx *rctx, obj extra_args, enum machine_kind m)
{
	obj cmd = make_obj(wk, obj_array);

	obj pkgconfig_cmd_arr = 0;

	{
		obj prog;
		struct find_program_ctx ctx = {
			.res = &prog,
			.requirement = requirement_auto,
			.machine = m,
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


	const char *envstr;
	uint32_t envc;
	{
		obj env = make_obj_environment(wk, make_obj_environment_flag_no_default_vars);
		obj opt;
		get_option_value_for_machine_overridable(wk, current_project(wk), 0, "pkg_config_path", m, &opt);
		environment_set(wk, env, environment_set_mode_set, make_str(wk, "PKG_CONFIG_PATH"), opt, 0);
		env_to_envstr(wk, &envstr, &envc, env);
	}

	return run_cmd_checked(wk, rctx, argstr, argc, envstr, envc);
}

static bool
pkgconfig_exec_lookup(struct workspace *wk, struct pkgconfig_info *info)
{
	L("pkg-config-exec: looking up %s %s", get_cstr(wk, info->name), info->is_static ? "static" : "dynamic");

	{
		obj args = make_obj(wk, obj_array);
		obj_array_push(wk, args, make_str(wk, "--modversion"));
		obj_array_push(wk, args, info->name);

		struct run_cmd_ctx rctx = { 0 };
		bool ok = pkgconfig_cmd(wk, &rctx, args, info->for_machine);
		if (ok) {
			tstr_trim_trailing_newline(&rctx.out);
			cstr_copy(info->version, &TSTR_STR(&rctx.out));
		}
		run_cmd_ctx_destroy(&rctx);
		if (!ok) {
			return false;
		}
	}

	const char *flag_type_str[] = {
		[muon_pkgconfig_fragment_source_cflags] = "--cflags",
		[muon_pkgconfig_fragment_source_libs] = "--libs"
	};

	for (uint32_t flag_type = 0; flag_type < ARRAY_LEN(flag_type_str); ++flag_type) {
		obj args = make_obj(wk, obj_array);
		obj_array_push(wk, args, make_str(wk, flag_type_str[flag_type]));
		if (info->is_static) {
			obj_array_push(wk, args, make_str(wk, "--static"));
		}
		obj_array_push(wk, args, info->name);

		struct run_cmd_ctx rctx = { 0 };
		bool ok = pkgconfig_cmd(wk, &rctx, args, info->for_machine);
		if (!ok) {
			goto cleanup;
		}

		char type = 0;
		obj arg, split = str_shell_split(wk, &TSTR_STR(&rctx.out), shell_type_posix);
		obj_array_for(wk, split, arg) {
			if (!type) {
				const struct str *arg_str = get_str(wk, arg);
				if (arg_str->s[0] == '-' && arg_str->len > 1) {
					if (strchr("Ll", arg_str->s[1])) {
						type = arg_str->s[1];
						if (arg_str->len > 2) {
							arg = make_strn(wk, arg_str->s + 2, arg_str->len - 2);
						} else {
							continue;
						}
					}
				}
			}

			struct muon_pkgconfig_fragment frag = {
				.source = flag_type,
				.type = type,
				.data = arg,
			};
			muon_pkgconfig_parse_fragment(wk, &frag, info);

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
pkgconfig_exec_get_variable(struct workspace *wk, obj pkg_name, obj var_name, obj defines, enum machine_kind m, obj *res)
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
	bool ok = pkgconfig_cmd(wk, &rctx, args, m);
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
