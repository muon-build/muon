/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "args.h"
#include "error.h"
#include "functions/external_program.h"
#include "guess.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

obj
obj_external_program_cmd_array(struct workspace *wk,
	struct obj_external_program *ep,
	enum obj_external_program_cmd_array_flag flags)
{
	if (ep->impl.build_target) {
		switch (get_obj_type(wk, ep->impl.build_target)) {
		case obj_build_target: {
			struct obj_build_target *tgt = get_obj_build_target(wk, ep->impl.build_target);
			ep->impl.cmd_array = make_obj(wk, obj_array);
			TSTR(abs);
			path_join(wk, &abs, get_cstr(wk, tgt->build_dir), get_cstr(wk, tgt->build_name));
			obj_array_push(wk, ep->impl.cmd_array, tstr_into_str(wk, &abs));
			break;
		}
		case obj_custom_target: {
			struct obj_custom_target *tgt = get_obj_custom_target(wk, ep->impl.build_target);
			ep->impl.cmd_array = make_obj(wk, obj_array);
			obj v;
			obj_array_for(wk, tgt->output, v) {
				obj_array_push(wk, ep->impl.cmd_array, *get_obj_file(wk, v));
			}
			break;
		}
		default: UNREACHABLE;
		}
	}

	if (ep->impl.cmd_array) {
		return ep->impl.cmd_array;
	} else {
		UNREACHABLE_RETURN;
	}
}

void
find_program_guess_version(struct workspace *wk, struct obj_external_program *ep, obj version_argument)
{
	if (ep->ver) {
		return;
	}

	obj args;
	obj_array_dup(wk, obj_external_program_cmd_array(wk, ep, 0), &args);
	obj_array_push(wk, args, version_argument ? version_argument : make_str(wk, "--version"));

	const char *argstr;
	uint32_t argc;
	join_args_argstr(wk, &argstr, &argc, args);

	bool got_version = false;
	struct run_cmd_ctx cmd_ctx = { 0 };
	if (run_cmd(wk, &cmd_ctx, argstr, argc, NULL, 0) && cmd_ctx.status == 0) {
		if (guess_version(wk, cmd_ctx.out.buf, &ep->ver)) {
			got_version = true;
		}
	}
	run_cmd_ctx_destroy(&cmd_ctx);

	if (!got_version) {
		ep->ver = make_str(wk, "unknown");
	}
}

FUNC_IMPL(external_program, cmd_array, tc_array, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = obj_external_program_cmd_array(wk, get_obj_external_program(wk, self), 0);
	return true;
}

FUNC_IMPL(external_program, found, tc_bool, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, get_obj_external_program(wk, self)->found);
	return true;
}

FUNC_IMPL(external_program, full_path, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct obj_external_program *ep = get_obj_external_program(wk, self);

	if (ep->impl.original_argv0) {
		*res = ep->impl.original_argv0;
		return true;
	}

	obj cmd_array = obj_external_program_cmd_array(wk, get_obj_external_program(wk, self), 0);
	if (get_obj_array(wk, cmd_array)->len > 1) {
		vm_error(wk,
			"cannot return the full_path() of an external program with multiple elements (have: %o)\n",
			cmd_array);
		return false;
	}

	*res = obj_array_get_head(wk, cmd_array);
	return true;
}

FUNC_IMPL(external_program, version, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct obj_external_program *ep = get_obj_external_program(wk, self);
	find_program_guess_version(wk, ep, 0);
	*res = ep->ver;
	return true;
}

FUNC_REGISTER(external_program)
{
	FUNC_IMPL_REGISTER(external_program, cmd_array);
	FUNC_IMPL_REGISTER(external_program, found);
	FUNC_IMPL_REGISTER(external_program, full_path);
	FUNC_IMPL_REGISTER(external_program, version);
	FUNC_IMPL_REGISTER_ALIAS(external_program, full_path, path);
}
