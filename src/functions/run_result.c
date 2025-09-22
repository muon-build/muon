/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "functions/run_result.h"
#include "lang/typecheck.h"

static bool
ensure_valid_run_result(struct workspace *wk, obj self, enum log_level lvl)
{
	struct obj_run_result *rr = get_obj_run_result(wk, self);

	if ((rr->flags & run_result_flag_from_compile) && !(rr->flags & run_result_flag_compile_ok)) {
		vm_diagnostic(wk, 0, lvl, 0, "this run_result was not run because its source could not be compiled");
		return false;
	}

	return true;
}

FUNC_IMPL(run_result, returncode, tc_number, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	if (!ensure_valid_run_result(wk, self, log_warn)) {
		*res = make_obj(wk, obj_number);
		set_obj_number(wk, *res, -1);
		return true;
	}

	*res = make_obj(wk, obj_number);
	set_obj_number(wk, *res, get_obj_run_result(wk, self)->status);
	return true;
}

FUNC_IMPL(run_result, stdout, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	if (!ensure_valid_run_result(wk, self, log_error)) {
		return false;
	}

	*res = get_obj_run_result(wk, self)->out;
	return true;
}

FUNC_IMPL(run_result, stderr, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	if (!ensure_valid_run_result(wk, self, log_error)) {
		return false;
	}

	*res = get_obj_run_result(wk, self)->err;
	return true;
}

FUNC_IMPL(run_result, compiled, tc_bool, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct obj_run_result *rr = get_obj_run_result(wk, self);

	if (!(rr->flags & run_result_flag_from_compile)) {
		vm_error(wk, "this run_result is not from a compiler.run() call");
		return false;
	}

	*res = make_obj_bool(wk, rr->flags & run_result_flag_compile_ok);
	return true;
}

FUNC_REGISTER(run_result)
{
	FUNC_IMPL_REGISTER(run_result, compiled);
	FUNC_IMPL_REGISTER(run_result, returncode);
	FUNC_IMPL_REGISTER(run_result, stderr);
	FUNC_IMPL_REGISTER(run_result, stdout);
}
