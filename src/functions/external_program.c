/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include "functions/common.h"
#include "functions/external_program.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_external_program_found(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, get_obj_external_program(wk, rcvr)->found);
	return true;
}

static bool
func_external_program_path(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_external_program(wk, rcvr)->full_path;
	return true;
}

static bool
func_external_program_version(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_external_program(wk, rcvr)->ver;
	return true;
}

const struct func_impl_name impl_tbl_external_program[] = {
	{ "found", func_external_program_found, tc_bool },
	{ "path", func_external_program_path, tc_string },
	{ "full_path", func_external_program_path, tc_string },
	{ "version", func_external_program_version, tc_string },
	{ NULL, NULL },
};
