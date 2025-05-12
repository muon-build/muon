/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_EXTERNAL_LIBPKGCONF_H
#define MUON_EXTERNAL_LIBPKGCONF_H

#include "lang/workspace.h"

#define MAX_VERSION_LEN 32

struct pkgconfig_info {
	char version[MAX_VERSION_LEN + 1];
	obj includes, libs, not_found_libs, link_args, compile_args;
};

enum pkgconfig_impl_type {
	pkgconfig_impl_type_null,
	pkgconfig_impl_type_exec,
	pkgconfig_impl_type_libpkgconf,
};
#define pkgconfig_impl_type_count 3

struct pkgconfig_impl {
	bool (*lookup)(struct workspace *wk, obj compiler, obj name, bool is_static, struct pkgconfig_info *info);
	bool (*get_variable)(struct workspace *wk, obj pkg_name, obj var_name, obj defines, obj *res);
};

extern const struct pkgconfig_impl pkgconfig_impl_null;
extern const struct pkgconfig_impl pkgconfig_impl_exec;
extern const struct pkgconfig_impl pkgconfig_impl_libpkgconf;

extern struct pkgconfig_impl pkgconfig_impls[pkgconfig_impl_type_count];

void muon_pkgconfig_init(struct workspace *wk);
bool muon_pkgconfig_set_impl_type(struct workspace *wk, enum pkgconfig_impl_type t);
const char *muon_pkgconfig_impl_type_to_s(enum pkgconfig_impl_type t);
bool muon_pkgconfig_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, struct pkgconfig_info *info);
bool muon_pkgconfig_get_variable(struct workspace *wk, obj pkg_name, obj var_name, obj defines, obj *res);
#endif
