/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_EXTERNAL_LIBPKGCONF_H
#define MUON_EXTERNAL_LIBPKGCONF_H

#include "lang/workspace.h"

#define MAX_VERSION_LEN 32

struct pkgconf_info {
	char version[MAX_VERSION_LEN + 1];
	obj includes, libs, not_found_libs, link_args, compile_args;
};

extern const bool have_libpkgconf;

bool muon_pkgconf_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, struct pkgconf_info *info);
bool muon_pkgconf_get_variable(struct workspace *wk, const char *pkg_name, const char *var, obj *res);
bool muon_pkgconf_define(struct workspace *wk, const char *key, const char *value);
#endif
