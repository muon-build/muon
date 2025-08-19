/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "formats/json.h"
#include "functions/modules/json.h"
#include "lang/typecheck.h"

FUNC_IMPL(module_json, parse, tc_array | tc_dict, .desc = "Parse a json string into an object")
{
	struct args_norm an[] = {
		{ tc_string, .desc = "the json to parse" },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_check,
	};
	struct args_kw akw[] = {
		[kw_check] = { "check", obj_bool },
		0
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	bool check = get_obj_bool_with_default(wk, akw[kw_check].val, true);

	obj r;
	bool ok = muon_json_to_obj(wk, get_str(wk, an[0].val), &r);

	if (check) {
		if (!ok) {
			vm_error(wk, "failed to parse json: %s", get_str(wk, r)->s);
			return false;
		}

		*res = r;
	} else {
		*res = make_obj(wk, obj_dict);
		obj_dict_set(wk, *res, make_str(wk, "ok"), make_obj_bool(wk, ok));
		obj_dict_set(wk, *res, make_str(wk, "result"), r);
	}
	return true;
}

FUNC_IMPL(module_json, stringify, tc_any, .desc = "Convert an object into a json string")
{
	struct args_norm an[] = {
		{ tc_array | tc_dict, .desc = "the object to stringify" },
		ARG_TYPE_NULL,
	};
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(buf);
	if (!obj_to_json(wk, an[0].val, &buf)) {
		return false;
	}

	*res = tstr_into_str(wk, &buf);
	return true;
}

FUNC_REGISTER(module_json)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_json, parse);
		FUNC_IMPL_REGISTER(module_json, stringify);
	}
}
