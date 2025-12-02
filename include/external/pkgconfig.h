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

	// Used internally during lookup
	obj name, libdirs, compiler;
	enum machine_kind for_machine;
	bool is_static;
};

enum pkgconfig_impl_type {
	pkgconfig_impl_type_null,
	pkgconfig_impl_type_exec,
	pkgconfig_impl_type_libpkgconf,
};
#define pkgconfig_impl_type_count 3

struct pkgconfig_impl {
	bool (*lookup)(struct workspace *wk, struct pkgconfig_info *info);
	bool (*get_variable)(struct workspace *wk, obj pkg_name, obj var_name, obj defines, enum machine_kind machine, obj *res);
};

extern const struct pkgconfig_impl pkgconfig_impl_null;
extern const struct pkgconfig_impl pkgconfig_impl_exec;
extern const struct pkgconfig_impl pkgconfig_impl_libpkgconf;

extern struct pkgconfig_impl pkgconfig_impls[pkgconfig_impl_type_count];

void muon_pkgconfig_init(struct workspace *wk);
bool muon_pkgconfig_set_impl_type(struct workspace *wk, enum pkgconfig_impl_type t);
const char *muon_pkgconfig_impl_type_to_s(enum pkgconfig_impl_type t);
bool muon_pkgconfig_lookup(struct workspace *wk, obj compiler, obj name, bool is_static, enum machine_kind for_machine, struct pkgconfig_info *info);
bool muon_pkgconfig_get_variable(struct workspace *wk,
	obj pkg_name,
	obj var_name,
	obj defines,
	enum machine_kind machine,
	obj *res);

enum muon_pkgconfig_fragment_source {
	muon_pkgconfig_fragment_source_cflags,
	muon_pkgconfig_fragment_source_libs,
};

struct muon_pkgconfig_fragment {
	enum muon_pkgconfig_fragment_source source;
	obj data;
	char type;
};

bool muon_pkgconfig_parse_fragment(struct workspace *wk,
	const struct muon_pkgconfig_fragment *frag,
	struct pkgconfig_info *info);
#endif
