#include "posix.h"

#include "external/libpkgconf.h"
#include "log.h"

const bool have_libpkgconf = false;

void
muon_pkgconf_deinit(void)
{
}

void
muon_pkgconf_init(struct workspace *wk)
{
}

bool
muon_pkgconf_lookup(struct workspace *wk, obj name, bool is_static, struct pkgconf_info *info)
{
	LOG_W("libpkgconf not enabled");
	return false;
}

bool
muon_pkgconf_get_variable(struct workspace *wk, const char *pkg_name, const char *var, obj *res)
{
	LOG_W("libpkgconf not enabled");
	return false;
}
