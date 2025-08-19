/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "lang/func_lookup.h"
#include "functions/feature_opt.h"
#include "lang/typecheck.h"

static bool
feature_opt_common(struct workspace *wk, obj self, obj *res, enum feature_opt_state state)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, get_obj_feature_opt(wk, self) == state);
	return true;
}

FUNC_IMPL(feature_opt, auto, tc_bool)
{
	return feature_opt_common(wk, self, res, feature_opt_auto);
}

FUNC_IMPL(feature_opt, disabled, tc_bool)
{
	return feature_opt_common(wk, self, res, feature_opt_disabled);
}

FUNC_IMPL(feature_opt, enabled, tc_bool)
{
	return feature_opt_common(wk, self, res, feature_opt_enabled);
}

FUNC_IMPL(feature_opt, allowed, tc_bool)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	enum feature_opt_state state = get_obj_feature_opt(wk, self);

	*res = make_obj_bool(wk, state == feature_opt_auto || state == feature_opt_enabled);
	return true;
}

FUNC_IMPL(feature_opt, disable_auto_if, tc_feature_opt)
{
	struct args_norm an[] = { { tc_bool }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	enum feature_opt_state state = get_obj_feature_opt(wk, self);

	if (!get_obj_bool(wk, an[0].val)) {
		*res = self;
		return true;
	} else if (state == feature_opt_disabled || state == feature_opt_enabled) {
		*res = self;
		return true;
	} else {
		*res = make_obj(wk, obj_feature_opt);
		set_obj_feature_opt(wk, *res, feature_opt_disabled);
		return true;
	}
}

FUNC_IMPL(feature_opt, enable_auto_if, tc_feature_opt)
{
	struct args_norm an[] = { { tc_bool }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	enum feature_opt_state state = get_obj_feature_opt(wk, self);

	if (!get_obj_bool(wk, an[0].val)) {
		*res = self;
		return true;
	} else if (state == feature_opt_disabled || state == feature_opt_enabled) {
		*res = self;
		return true;
	} else {
		*res = make_obj(wk, obj_feature_opt);
		set_obj_feature_opt(wk, *res, feature_opt_enabled);
		return true;
	}
}

FUNC_IMPL(feature_opt, enable_if, tc_feature_opt)
{
	struct args_norm an[] = { { tc_bool }, ARG_TYPE_NULL };
	enum kwargs {
		kw_error_message,
	};
	struct args_kw akw[] = { [kw_error_message] = { "error_message", obj_string }, 0 };
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	enum feature_opt_state state = get_obj_feature_opt(wk, self);

	if (!get_obj_bool(wk, an[0].val)) {
		*res = self;
		return true;
	} else if (state == feature_opt_disabled) {
		const char *err_msg = akw[kw_error_message].set ? get_cstr(wk, akw[kw_error_message].set) :
								  "requirement not met";

		vm_error_at(wk, an[0].node, "%s", err_msg);
		return false;
	} else {
		*res = make_obj(wk, obj_feature_opt);
		set_obj_feature_opt(wk, *res, feature_opt_enabled);
		return true;
	}

	return true;
}

FUNC_IMPL(feature_opt, disable_if, tc_feature_opt)
{
	struct args_norm an[] = { { tc_bool }, ARG_TYPE_NULL };
	enum kwargs {
		kw_error_message,
	};
	struct args_kw akw[] = { [kw_error_message] = { "error_message", obj_string }, 0 };
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	enum feature_opt_state state = get_obj_feature_opt(wk, self);

	if (!get_obj_bool(wk, an[0].val)) {
		*res = self;
		return true;
	} else if (state == feature_opt_enabled) {
		const char *err_msg = akw[kw_error_message].set ? get_cstr(wk, akw[kw_error_message].set) :
								  "requirement not met";

		vm_error_at(wk, an[0].node, "%s", err_msg);
		return false;
	} else {
		*res = make_obj(wk, obj_feature_opt);
		set_obj_feature_opt(wk, *res, feature_opt_disabled);
		return true;
	}

	return true;
}

FUNC_IMPL(feature_opt, require, tc_feature_opt)
{
	struct args_norm an[] = { { tc_bool }, ARG_TYPE_NULL };
	enum kwargs {
		kw_error_message,
	};
	struct args_kw akw[] = { [kw_error_message] = { "error_message", obj_string }, 0 };

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	enum feature_opt_state state = get_obj_feature_opt(wk, self);
	if (!get_obj_bool(wk, an[0].val)) {
		if (state == feature_opt_enabled) {
			vm_error_at(wk,
				an[0].node,
				"%s",
				akw[kw_error_message].set ? get_cstr(wk, akw[kw_error_message].set) :
							    "requirement not met");
			return false;
		} else {
			*res = make_obj(wk, obj_feature_opt);
			set_obj_feature_opt(wk, *res, feature_opt_disabled);
		}
	} else {
		*res = self;
	}

	return true;
}

FUNC_REGISTER(feature_opt)
{
	FUNC_IMPL_REGISTER(feature_opt, allowed);
	FUNC_IMPL_REGISTER(feature_opt, auto);
	FUNC_IMPL_REGISTER(feature_opt, disable_auto_if);
	FUNC_IMPL_REGISTER(feature_opt, disable_if);
	FUNC_IMPL_REGISTER(feature_opt, disabled);
	FUNC_IMPL_REGISTER(feature_opt, enable_auto_if);
	FUNC_IMPL_REGISTER(feature_opt, enable_if);
	FUNC_IMPL_REGISTER(feature_opt, enabled);
	FUNC_IMPL_REGISTER(feature_opt, require);
}
