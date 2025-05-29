/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@thunix.net>
 * SPDX-FileCopyrightText: Harley Swick <fancycade@mycanofbeans.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "error.h"
#include "external/pkgconfig.h"
#include "functions/dependency.h"
#include "functions/kernel/dependency.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "platform/assert.h"
#include "platform/path.h"

static bool
func_dependency_found(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, (get_obj_dependency(wk, self)->flags & dep_flag_found) == dep_flag_found);
	return true;
}

static bool
dep_get_pkgconfig_variable(struct workspace *wk, obj dep, uint32_t node, obj var, obj defines, obj *res)
{
	struct obj_dependency *d = get_obj_dependency(wk, dep);
	if (d->type != dependency_type_pkgconf) {
		vm_error_at(wk, node, "dependency not from pkgconf");
		return false;
	}

	if (!muon_pkgconfig_get_variable(wk, d->name, var, defines, res)) {
		return false;
	}
	return true;
}

static bool
func_dependency_get_pkgconfig_variable(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_default,
	};
	struct args_kw akw[] = { [kw_default] = { "default", obj_string }, 0 };
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (!dep_get_pkgconfig_variable(wk, self, an[0].node, an[0].val, 0, res)) {
		if (akw[kw_default].set) {
			*res = akw[kw_default].val;
		} else {
			vm_error_at(wk, an[0].node, "undefined pkg_config variable");
			return false;
		}
	}

	return true;
}

static bool
func_dependency_get_configtool_variable(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, 0)) {
		return false;
	}

	vm_error_at(wk, 0, "get_configtool_variable not implemented");
	return false;
}

static bool
dep_pkgconfig_define(struct workspace *wk, obj dep, uint32_t node, obj var, obj *res)
{
	struct obj_array *arr = get_obj_array(wk, var);
	if (arr->len % 2 != 0) {
		vm_error_at(wk, node, "non-even number of arguments in list");
		return false;
	}

	*res = make_obj(wk, obj_dict);

	obj key = 0, val = 0;
	obj_array_for_array_(wk, arr, val, iter) {
		if (!(iter.i & 1)) {
			key = val;
			continue;
		}

		obj_dict_set(wk, *res, key, val);
	}

	return true;
}

static enum dependency_public_type
dependency_type_to_public_type(struct obj_dependency *d)
{
	if (d->public_type != dependency_public_type_unset) {
		return d->public_type;
	}

	switch (d->type) {
	case dependency_type_pkgconf: return dependency_public_type_pkgconfig; break;
	case dependency_type_declared: return dependency_public_type_internal; break;
	case dependency_type_system:
	case dependency_type_threads: return dependency_public_type_system; break;
	case dependency_type_external_library: return dependency_public_type_library; break;
	case dependency_type_not_found: return dependency_public_type_not_found; break;
	default: UNREACHABLE_RETURN;
	}
}

static bool
func_dependency_get_variable(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string, .optional = true }, ARG_TYPE_NULL };
	enum kwargs {
		kw_pkgconfig,
		kw_pkgconfig_define,
		kw_internal,
		kw_system,
		kw_default_value,
	};
	struct args_kw akw[] = {
		[kw_pkgconfig] = { "pkgconfig", obj_string },
		[kw_pkgconfig_define] = { "pkgconfig_define", TYPE_TAG_LISTIFY | obj_string },
		[kw_internal] = { "internal", obj_string },
		[kw_system] = { "system", obj_string },
		[kw_default_value] = { "default_value", obj_string },
		0,
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	uint32_t type_kwargs[] = { kw_pkgconfig, kw_internal, kw_system };

	if (an[0].set) {
		uint32_t i;
		for (i = 0; i < ARRAY_LEN(type_kwargs); ++i) {
			uint32_t kw = type_kwargs[i];

			if (!akw[kw].set) {
				akw[kw].set = true;
				akw[kw].node = an[0].node;
				akw[kw].val = an[0].val;
			}
		}
	}

	struct obj_dependency *dep = get_obj_dependency(wk, self);

	switch (dependency_type_to_public_type(dep)) {
	case dependency_public_type_pkgconfig: {
		obj defines = 0;
		if (akw[kw_pkgconfig_define].set) {
			if (!dep_pkgconfig_define(
				    wk, self, akw[kw_pkgconfig_define].node, akw[kw_pkgconfig_define].val, &defines)) {
				return false;
			}
		}

		if (akw[kw_pkgconfig].set) {
			if (dep_get_pkgconfig_variable(
				    wk, self, akw[kw_pkgconfig].node, akw[kw_pkgconfig].val, defines, res)) {
				return true;
			}
		}
		break;
	}
	case dependency_public_type_internal: {
		if (dep->variables && akw[kw_internal].set) {
			if (obj_dict_index(wk, dep->variables, akw[kw_internal].val, res)) {
				return true;
			}
		}
		break;
	}
	case dependency_public_type_system: {
		if (dep->variables && akw[kw_system].set) {
			if (obj_dict_index(wk, dep->variables, akw[kw_system].val, res)) {
				return true;
			}
		}
		break;
	}
	case dependency_public_type_library: break;
	case dependency_public_type_not_found: break;
	case dependency_public_type_unset: break;
	}

	if (akw[kw_default_value].set) {
		*res = akw[kw_default_value].val;
		return true;
	} else {
		vm_error(wk, "dependency has no such variable");
		return false;
	}
}

static bool
func_dependency_version(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	obj version = get_obj_dependency(wk, self)->version;

	if (version) {
		*res = version;
	} else {
		*res = make_str(wk, "unknown");
	}

	return true;
}

static bool
func_dependency_type_name(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct obj_dependency *dep = get_obj_dependency(wk, self);

	if (!(dep->flags & dep_flag_found)) {
		*res = make_str(wk, "not-found");
		return true;
	}

	const char *n = NULL;
	switch (dependency_type_to_public_type(dep)) {
	case dependency_public_type_pkgconfig: n = "pkgconfig"; break;
	case dependency_public_type_internal: n = "internal"; break;
	case dependency_public_type_system: n = "system"; break;
	case dependency_public_type_library: n = "library"; break;
	case dependency_public_type_not_found: n = "not-found"; break;
	case dependency_public_type_unset: UNREACHABLE; break;
	}

	*res = make_str(wk, n);
	return true;
}

static bool
func_dependency_name(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct obj_dependency *dep = get_obj_dependency(wk, self);

	if (!dep->name) {
		*res = make_str(wk, "internal");
	} else {
		*res = dep->name;
	}

	return true;
}

static bool
func_dependency_partial_dependency(struct workspace *wk, obj self, obj *res)
{
	enum kwargs {
		kw_compile_args,
		kw_includes,
		kw_link_args,
		kw_links,
		kw_sources,
	};
	struct args_kw akw[] = { [kw_compile_args] = { "compile_args", obj_bool },
		[kw_includes] = { "includes", obj_bool },
		[kw_link_args] = { "link_args", obj_bool },
		[kw_links] = { "links", obj_bool },
		[kw_sources] = { "sources", obj_bool },
		0 };
	if (!pop_args(wk, NULL, akw)) {
		return false;
	}

	enum build_dep_flag flags = build_dep_flag_recursive | build_dep_flag_partial;

	if (akw[kw_compile_args].set && get_obj_bool(wk, akw[kw_compile_args].val)) {
		flags |= build_dep_flag_part_compile_args;
	}

	if (akw[kw_includes].set && get_obj_bool(wk, akw[kw_includes].val)) {
		flags |= build_dep_flag_part_includes;
	}

	if (akw[kw_link_args].set && get_obj_bool(wk, akw[kw_link_args].val)) {
		flags |= build_dep_flag_part_link_args;
	}

	if (akw[kw_links].set && get_obj_bool(wk, akw[kw_links].val)) {
		flags |= build_dep_flag_part_links;
	}

	if (akw[kw_sources].set && get_obj_bool(wk, akw[kw_sources].val)) {
		flags |= build_dep_flag_part_sources;
	}

	if (!(*res = dependency_dup(wk, self, flags))) {
		return false;
	}

	return true;
}

static bool
func_dependency_as_system(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_string, .optional = true }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	enum include_type inc_type = include_type_system;
	if (an[0].set) {
		if (!coerce_include_type(wk, get_str(wk, an[0].val), an[0].node, &inc_type)) {
			return false;
		}
	}

	enum build_dep_flag flags = 0;

	switch (inc_type) {
	case include_type_preserve: break;
	case include_type_system: flags |= build_dep_flag_include_system; break;
	case include_type_non_system: flags |= build_dep_flag_include_non_system; break;
	}

	if (!(*res = dependency_dup(wk, self, flags))) {
		return false;
	}

	get_obj_dependency(wk, *res)->include_type = inc_type;

	return true;
}

static bool
func_dependency_as_link_whole(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	if (!(*res = dependency_dup(wk, self, build_dep_flag_as_link_whole))) {
		return false;
	}

	return true;
}

static bool
func_dependency_both_libs_common(struct workspace *wk, obj self, obj *res, enum build_dep_flag flags)
{
	enum kwargs {
		kw_recursive,
	};
	struct args_kw akw[] = {
		[kw_recursive] = { "recursive", obj_bool },
		0,
	};
	if (!pop_args(wk, 0, akw)) {
		return false;
	}

	if (get_obj_bool_with_default(wk, akw[kw_recursive].val, false)) {
		flags |= build_dep_flag_recursive;
	}

	if (!(*res = dependency_dup(wk, self, flags))) {
		return false;
	}

	return true;
}

static bool
func_dependency_as_shared(struct workspace *wk, obj self, obj *res)
{
	return func_dependency_both_libs_common(wk, self, res, build_dep_flag_both_libs_shared);
}

static bool
func_dependency_as_static(struct workspace *wk, obj self, obj *res)
{
	return func_dependency_both_libs_common(wk, self, res, build_dep_flag_both_libs_static);
}

static bool
func_dependency_include_type(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	const char *s = NULL;
	switch (get_obj_dependency(wk, self)->include_type) {
	case include_type_preserve: s = "preserve"; break;
	case include_type_system: s = "system"; break;
	case include_type_non_system: s = "non-system"; break;
	default: assert(false && "unreachable"); break;
	}

	*res = make_str(wk, s);
	return true;
}

const struct func_impl impl_tbl_dependency[] = {
	{ "as_link_whole", func_dependency_as_link_whole, tc_dependency },
	{ "as_shared", func_dependency_as_shared, tc_dependency },
	{ "as_static", func_dependency_as_static, tc_dependency },
	{ "as_system", func_dependency_as_system, tc_dependency },
	{ "found", func_dependency_found, tc_bool },
	{ "get_configtool_variable", func_dependency_get_configtool_variable, tc_string },
	{ "get_pkgconfig_variable", func_dependency_get_pkgconfig_variable, tc_string },
	{ "get_variable", func_dependency_get_variable, tc_string },
	{ "include_type", func_dependency_include_type, tc_string },
	{ "name", func_dependency_name, tc_string },
	{ "partial_dependency", func_dependency_partial_dependency, tc_dependency },
	{ "type_name", func_dependency_type_name, tc_string },
	{ "version", func_dependency_version, tc_string },
	{ NULL, NULL },
};
