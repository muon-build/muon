/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "error.h"
#include "external/pkgconfig.h"
#include "functions/compiler.h"
#include "lang/object_iterators.h"
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

void
muon_pkgconfig_info_init(struct workspace *wk,
	obj compiler,
	obj name,
	bool is_static,
	enum machine_kind for_machine,
	struct pkgconfig_info *info)
{
	*info = (struct pkgconfig_info){ 0 };
	info->name = name;
	info->link_with = make_obj(wk, obj_array);
	info->link_with_raw = make_obj(wk, obj_array);
	info->libdirs = make_obj(wk, obj_array);
	info->compiler = compiler;
	info->is_static = is_static;
	info->for_machine = for_machine;
}

bool
muon_pkgconfig_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, enum machine_kind for_machine, struct pkgconfig_info *info)
{
	muon_pkgconfig_init(wk);
	muon_pkgconfig_info_init(wk, compiler, name, is_static, for_machine, info);

	info->compile_args = make_obj(wk, obj_array);
	info->link_args = make_obj(wk, obj_array);

	return pkgconfig_impls[pkgconfig_state.type].lookup(wk, info);
}

bool
muon_pkgconfig_get_variable(struct workspace *wk,
	obj pkg_name,
	obj var_name,
	obj defines,
	enum machine_kind machine,
	obj *res)
{
	muon_pkgconfig_init(wk);

	return pkgconfig_impls[pkgconfig_state.type].get_variable(wk, pkg_name, var_name, defines, machine, res);
}

void
muon_pkgconfig_parse_fragment(struct workspace *wk, const struct muon_pkgconfig_fragment *frag, struct pkgconfig_info *info, obj default_dest)
{
	switch (frag->type) {
	case 'L': {
		if (info->raw) {
			goto add_raw;
		} else {
			if (!info->libdirs) {
				make_obj(wk, info->libdirs);
			}
			obj_array_push(wk, info->libdirs, frag->data);
		}
		break;
	}
	case 'l': {
		enum find_library_flag flags = info->is_static ? find_library_flag_prefer_static : 0;
		struct find_library_result find_result
			= find_library(wk, info->compiler, get_str(wk, frag->data)->s, info->libdirs, flags);
		if (find_result.found) {
			if (find_result.location == find_library_found_location_link_arg || info->raw) {
				obj_array_push(wk, info->link_with_raw, frag->data);
			} else {
				obj_array_push(wk, info->link_with, find_result.found);
			}
		} else {
			if (info->name) {
				LOG_W("pkg-config-exec: dependency '%s' missing required library '%s'",
					get_cstr(wk, info->name),
					get_cstr(wk, frag->data));
			}
			obj_array_push(wk, info->link_with_raw, frag->data);
		}
		break;
	}
add_raw:
	default: {
		obj arg = frag->data;
		if (frag->type) {
			arg = make_strf(wk, "-%c%s", frag->type, get_cstr(wk, frag->data));
		}
		obj_array_push(wk, default_dest, arg);
		break;
	}
	}
}

void
muon_pkgconfig_parse_fragment_array(struct workspace *wk,
	struct pkgconfig_info *info,
	obj array,
	obj default_dest)
{
	char type = 0;
	obj arg;
	obj_array_for(wk, array, arg) {
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
			.type = type,
			.data = arg,
		};
		muon_pkgconfig_parse_fragment(wk, &frag, info, default_dest);

		type = 0;
	}
}

/*-----------------------------------------------------------------------------
 * pkgconfig null impl
 *---------------------------------------------------------------------------*/

static bool
pkgconfig_null_lookup(struct workspace *wk, struct pkgconfig_info *info)
{
	LOG_W("pkg-config support not enabled");
	return false;
}

static bool
pkgconfig_null_get_variable(struct workspace *wk, obj pkg_name, obj var_name, obj defines, enum machine_kind m, obj *res)
{
	LOG_W("pkg-config support not enabled");
	return false;
}

const struct pkgconfig_impl pkgconfig_impl_null = {
	.lookup = pkgconfig_null_lookup,
	.get_variable = pkgconfig_null_get_variable,
};
