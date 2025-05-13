/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "error.h"
#include "external/pkgconfig.h"
#include "options.h"
#include "platform/assert.h"

struct pkgconfig_impl pkgconfig_impls[pkgconfig_impl_type_count] = { 0 };

static struct pkgconfig_state {
	enum pkgconfig_impl_type type;
	enum option_value_source type_source;
	bool init;
} pkgconfig_state = { 0 };

#if !defined(MUON_HAVE_LIBPKGCONF)
const struct pkgconfig_impl pkgconfig_impl_libpkgconf = { 0 };
#endif

static void
muon_pkgconfig_check_override(struct workspace *wk)
{
	if (!wk) {
		return;
	}

	/* Check for pkg-config find_program override iff
	 * - our type isn't already set to exec
	 * - and the option was set with a lower precedence than default (i.e. the
	 *   user didn't specify anything).
	 *
	 * If an override is found then set the impl type to the exec backend so
	 * that it can take effect.
	 */
	if (pkgconfig_state.type == pkgconfig_impl_type_libpkgconf
		&& pkgconfig_state.type_source <= option_value_source_default) {
		obj _res;
		if (obj_dict_index_str(wk, wk->find_program_overrides[machine_kind_host], "pkg-config", &_res)) {
			// Don't use muon_pkgconfig_set_impl_type here or it will
			// infinitely recurse
			pkgconfig_state.type = pkgconfig_impl_type_exec;
			pkgconfig_state.type_source = option_value_source_environment;
		}
	}
}

void
muon_pkgconfig_init(struct workspace *wk)
{
	if (pkgconfig_state.init) {
		muon_pkgconfig_check_override(wk);
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
	struct obj_option *muon_pkgconfig;
	{
		obj res;
		get_option(wk, current_project(wk), &STRL("muon.pkgconfig"), &res);
		muon_pkgconfig = get_obj_option(wk, res);
		opt = get_str(wk, muon_pkgconfig->val);
	}

	if (str_eql(&STR("auto"), opt)) {
		if (!muon_pkgconfig_set_impl_type(wk, pkgconfig_impl_type_libpkgconf)) {
			muon_pkgconfig_set_impl_type(wk, pkgconfig_impl_type_exec);
		}

		pkgconfig_state.type_source = option_value_source_default;
	} else {
		enum pkgconfig_impl_type requested_t = pkgconfig_impl_type_null;
		if (str_eql(&STR("null"), opt)) {
			requested_t = pkgconfig_impl_type_null;
		} else if (str_eql(&STR("exec"), opt)) {
			requested_t = pkgconfig_impl_type_exec;
		} else if (str_eql(&STR("libpkgconf"), opt)) {
			requested_t = pkgconfig_impl_type_libpkgconf;
		}

		pkgconfig_state.type_source = muon_pkgconfig->source;

		if (!muon_pkgconfig_set_impl_type(wk, requested_t)) {
			LOG_W("pkgconfig impl %s is not available, falling back to exec",
				muon_pkgconfig_impl_type_to_s(requested_t));
			muon_pkgconfig_set_impl_type(wk, pkgconfig_impl_type_exec);
		}
	}

	muon_pkgconfig_check_override(wk);
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
