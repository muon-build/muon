/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "external/pkgconfig.h"
#include "log.h"

const bool have_libpkgconf = false;
const bool have_pkgconfig_exec = false;

bool
muon_pkgconf_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, struct pkgconf_info *info)
{
	LOG_W("pkg-config support not enabled");
	return false;
}

bool
muon_pkgconf_get_variable(struct workspace *wk, obj pkg_name, obj var_name, obj defines, obj *res)
{
	LOG_W("pkg-config support not enabled");
	return false;
}
