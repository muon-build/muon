/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "error.h"
#include "external/pkgconfig.h"
#include "options.h"
#include "platform/assert.h"

struct pkgconfig_impl pkgconfig_impls[pkgconfig_impl_type_count] = { 0 };

static struct pkgconfig_state {
	enum pkgconfig_impl_type type;
	bool init;
} pkgconfig_state = { 0 };

#if !defined(MUON_HAVE_LIBPKGCONF)
const struct pkgconfig_impl pkgconfig_impl_libpkgconf = { 0 };
#endif

void
muon_pkgconfig_init(struct workspace *wk)
{
	if (pkgconfig_state.init) {
		return;
	}
	pkgconfig_state.init = true;

	pkgconfig_impls[pkgconfig_impl_type_null] = pkgconfig_impl_null;
	pkgconfig_impls[pkgconfig_impl_type_exec] = pkgconfig_impl_exec;
	pkgconfig_impls[pkgconfig_impl_type_libpkgconf] = pkgconfig_impl_libpkgconf;

	if (!wk) {
		return;
	}

	const struct str *opt;
	{
		obj res;
		get_option_value(wk, current_project(wk), "muon.pkgconfig", &res);
		opt = get_str(wk, res);
	}

	enum pkgconfig_impl_type requested_t = pkgconfig_impl_type_null;
	if (str_eql(&STR("auto"), opt)) {
		if (!muon_pkgconfig_set_impl_type(wk, pkgconfig_impl_type_libpkgconf)) {
			muon_pkgconfig_set_impl_type(wk, pkgconfig_impl_type_exec);
		}
		return;
	} else if (str_eql(&STR("null"), opt)) {
		requested_t = pkgconfig_impl_type_null;
	} else if (str_eql(&STR("exec"), opt)) {
		requested_t = pkgconfig_impl_type_exec;
	} else if (str_eql(&STR("libpkgconf"), opt)) {
		requested_t = pkgconfig_impl_type_libpkgconf;
	}

	if (!muon_pkgconfig_set_impl_type(wk, requested_t)) {
		LOG_W("pkgconfig impl %s is not available, falling back to exec",
			muon_pkgconfig_impl_type_to_s(requested_t));
		muon_pkgconfig_set_impl_type(wk, pkgconfig_impl_type_exec);
	}
}

bool
muon_pkgconfig_set_impl_type(struct workspace *wk, enum pkgconfig_impl_type t)
{
	muon_pkgconfig_init(wk);

	if (pkgconfig_impls[t].get_variable) {
		pkgconfig_state.type = t;
		return true;
	}

	return false;
}

const char *
muon_pkgconfig_impl_type_to_s(enum pkgconfig_impl_type t)
{
	switch (t) {
	case pkgconfig_impl_type_null: return "null";
	case pkgconfig_impl_type_exec: return "exec";
	case pkgconfig_impl_type_libpkgconf: return "libpkgconf";
	default: UNREACHABLE_RETURN;
	}
}

bool
muon_pkgconfig_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, struct pkgconfig_info *info)
{
	muon_pkgconfig_init(wk);

	return pkgconfig_impls[pkgconfig_state.type].lookup(wk, compiler, name, is_static, info);
}

bool
muon_pkgconfig_get_variable(struct workspace *wk, obj pkg_name, obj var_name, obj defines, obj *res)
{
	muon_pkgconfig_init(wk);

	return pkgconfig_impls[pkgconfig_state.type].get_variable(wk, pkg_name, var_name, defines, res);
}

/*-----------------------------------------------------------------------------
 * pkgconfig null impl
 *---------------------------------------------------------------------------*/

static bool
pkgconfig_null_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, struct pkgconfig_info *info)
{
	LOG_W("pkg-config support not enabled");
	return false;
}

static bool
pkgconfig_null_get_variable(struct workspace *wk, obj pkg_name, obj var_name, obj defines, obj *res)
{
	LOG_W("pkg-config support not enabled");
	return false;
}

const struct pkgconfig_impl pkgconfig_impl_null = {
	.lookup = pkgconfig_null_lookup,
	.get_variable = pkgconfig_null_get_variable,
};
