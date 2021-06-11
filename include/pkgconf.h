#ifndef MUON_PKGCONF_H
#define MUON_PKGCONF_H

#include "workspace.h"

#define MAX_VERSION_LEN 32

struct pkgconf_info {
	char version[MAX_VERSION_LEN + 1];
	uint32_t includes, libs;
};

void pkgconf_deinit(void);
void pkgconf_init(void);
bool pkgconf_lookup(struct workspace *wk, const char *name, struct pkgconf_info *info);
bool pkgconf_get_variable(struct workspace *wk, const char *pkg_name, char *var, uint32_t *res);
#endif
