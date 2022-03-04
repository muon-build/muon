#ifndef MUON_EXTERNAL_LIBPKGCONF_H
#define MUON_EXTERNAL_LIBPKGCONF_H

#include "lang/workspace.h"

#define MAX_VERSION_LEN 32

struct pkgconf_info {
	char version[MAX_VERSION_LEN + 1];
	obj includes, libs, not_found_libs, link_args, compile_args;
};

extern const bool have_libpkgconf;

void muon_pkgconf_deinit(void);
void muon_pkgconf_init(struct workspace *wk);
bool muon_pkgconf_lookup(struct workspace *wk, obj name, bool is_static, struct pkgconf_info *info);
bool muon_pkgconf_get_variable(struct workspace *wk, const char *pkg_name, const char *var, obj *res);
#endif
