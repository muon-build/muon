#ifndef MUON_EXTERNAL_PKGCONF_H
#define MUON_EXTERNAL_PKGCONF_H

#include "lang/workspace.h"

#define MAX_VERSION_LEN 32

struct pkgconf_info {
	char version[MAX_VERSION_LEN + 1];
	uint32_t includes, libs;
};

extern const bool have_libpkgconf;

void muon_pkgconf_deinit(void);
void muon_pkgconf_init(void);
bool muon_pkgconf_lookup(struct workspace *wk, uint32_t name, bool is_static, struct pkgconf_info *info);
bool muon_pkgconf_get_variable(struct workspace *wk, const char *pkg_name, char *var, uint32_t *res);
#endif
