/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>

#include "args.h"
#include "error.h"
#include "functions/environment.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/os.h"
#include "platform/path.h"

obj
make_obj_environment(struct workspace *wk, enum make_obj_environment_flag flags)
{
	obj res = make_obj(wk, obj_environment);
	struct obj_environment *d = get_obj_environment(wk, res);
	d->actions = make_obj(wk, obj_array);

	if (!(flags & make_obj_environment_flag_no_default_vars)) {
		bool set_subdir = flags & make_obj_environment_flag_set_subdir;
		set_default_environment_vars(wk, res, set_subdir);
	}

	return res;
}

void
environment_extend(struct workspace *wk, obj env, obj other)
{
	struct obj_environment *a = get_obj_environment(wk, env), *b = get_obj_environment(wk, other);
	obj_array_extend(wk, a->actions, b->actions);
}

static enum iteration_result
evironment_to_dict_iter(struct workspace *wk, void *_ctx, obj action)
{
	obj env = *(obj *)_ctx, mode_num, key, val, sep;

	mode_num = obj_array_index(wk, action, 0);
	key = obj_array_index(wk, action, 1);
	val = obj_array_index(wk, action, 2);
	sep = obj_array_index(wk, action, 3);

	enum environment_set_mode mode = get_obj_number(wk, mode_num);

	if (mode == environment_set_mode_set) {
		obj_dict_set(wk, env, key, val);
		return ir_cont;
	}

	const char *oval;
	obj v;
	if (obj_dict_index(wk, env, key, &v)) {
		oval = get_cstr(wk, v);
	} else {
		if (!(oval = os_get_env(get_cstr(wk, key)))) {
			obj_dict_set(wk, env, key, val);
			return ir_cont;
		}
	}

	obj str;

	switch (mode) {
	case environment_set_mode_append:
		str = make_strf(wk, "%s%s%s", oval, get_cstr(wk, sep), get_cstr(wk, val));
		break;
	case environment_set_mode_prepend:
		str = make_strf(wk, "%s%s%s", get_cstr(wk, val), get_cstr(wk, sep), oval);
		break;
	default: UNREACHABLE;
	}

	obj_dict_set(wk, env, key, str);
	return ir_cont;
}

bool
environment_to_dict(struct workspace *wk, obj env, obj *res)
{
	if (get_obj_type(wk, env) == obj_dict) {
		*res = env;
		return true;
	}

	*res = make_obj(wk, obj_dict);

	return obj_array_foreach(wk, get_obj_environment(wk, env)->actions, res, evironment_to_dict_iter);
}

static void
environment_or_dict_set(struct workspace *wk, obj env, const char *key, const char *val)
{
	switch (get_obj_type(wk, env)) {
	case obj_dict: obj_dict_set(wk, env, make_str(wk, key), make_str(wk, val)); break;
	case obj_environment:
		environment_set(wk, env, environment_set_mode_set, make_str(wk, key), make_str(wk, val), 0);
		break;
	default: UNREACHABLE;
	}
}

void
set_default_environment_vars(struct workspace *wk, obj env, bool set_subdir)
{
	if (wk->vm.lang_mode == language_internal) {
		return;
	}

	if (wk->argv0) {
		// argv0 may not be set, e.g. during `muon install`
		environment_or_dict_set(wk, env, "MUON_PATH", wk->argv0);

		obj introspect = make_obj(wk, obj_array);
		obj_array_push(wk, introspect, make_str(wk, wk->argv0));
		obj_array_push(wk, introspect, make_str(wk, "meson"));
		obj_array_push(wk, introspect, make_str(wk, "introspect"));
		environment_or_dict_set(wk, env, "MESONINTROSPECT", get_str(wk, join_args_shell(wk, introspect))->s);
	}
	environment_or_dict_set(wk, env, "MESON_BUILD_ROOT", wk->build_root);
	environment_or_dict_set(wk, env, "MESON_SOURCE_ROOT", wk->source_root);

	if (set_subdir) {
		TSTR(subdir);
		path_relative_to(wk, &subdir, wk->source_root, get_cstr(wk, current_project(wk)->cwd));
		environment_or_dict_set(wk, env, "MESON_SUBDIR", subdir.buf);
	}
}

bool
environment_set(struct workspace *wk, obj env, enum environment_set_mode mode, obj key, obj vals, obj sep)
{
	if (!sep) {
		sep = make_str(wk, ENV_PATH_SEP_STR);
	}

	obj joined;
	if (get_obj_type(wk, vals) == obj_string) {
		joined = vals;
	} else {
		if (!obj_array_join(wk, false, vals, sep, &joined)) {
			return false;
		}
	}

	obj elem, mode_num;
	mode_num = make_obj(wk, obj_number);
	set_obj_number(wk, mode_num, mode);
	elem = make_obj(wk, obj_array);
	obj_array_push(wk, elem, mode_num);
	obj_array_push(wk, elem, key);
	obj_array_push(wk, elem, joined);
	obj_array_push(wk, elem, sep);

	obj_array_push(wk, get_obj_environment(wk, env)->actions, elem);
	return true;
}

static bool
func_environment_set_common(struct workspace *wk, obj self, enum environment_set_mode mode)
{
	struct args_norm an[] = { { obj_string }, { TYPE_TAG_GLOB | obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_separator,
	};
	struct args_kw akw[] = {
		[kw_separator] = { "separator", obj_string },
		0,
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (!get_obj_array(wk, an[1].val)->len) {
		vm_error_at(wk, an[1].node, "you must pass at least one value");
		return false;
	}

	return environment_set(wk, self, mode, an[0].val, an[1].val, akw[kw_separator].val);
}

FUNC_IMPL(environment, set, 0, func_impl_flag_impure)
{
	return func_environment_set_common(wk, self, environment_set_mode_set);
}

FUNC_IMPL(environment, append, 0, func_impl_flag_impure)
{
	return func_environment_set_common(wk, self, environment_set_mode_append);
}

FUNC_IMPL(environment, prepend, 0, func_impl_flag_impure)
{
	return func_environment_set_common(wk, self, environment_set_mode_prepend);
}

FUNC_IMPL(environment, unset, 0, func_impl_flag_impure)
{
	struct args_norm an[] = {
		{ obj_string, .desc = "The name to unset" },
		ARG_TYPE_NULL,
	};
	if (!pop_args(wk, an, 0)) {
		return false;
	}

	obj to_delete, action, actions = get_obj_environment(wk, self)->actions;

	to_delete = make_obj(wk, obj_array);

	uint32_t i = 0;
	obj_array_for(wk, actions, action) {
		if (obj_equal(wk, action, an[0].val)) {
			obj_array_push(wk, to_delete, i + 1);
		}

		++i;
	}

	obj_array_for(wk, to_delete, i) {
		obj_array_del(wk, actions, i - 1);
	}
	return true;
}

FUNC_REGISTER(environment)
{
	FUNC_IMPL_REGISTER(environment, set);
	FUNC_IMPL_REGISTER(environment, append);
	FUNC_IMPL_REGISTER(environment, prepend);
	FUNC_IMPL_REGISTER(environment, unset);
}
