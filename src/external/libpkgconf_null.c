#include "posix.h"

#include "external/libpkgconf.h"
#include "log.h"

const bool have_libpkgconf = false;

void
muon_pkgconf_deinit(void)
{
}

void
muon_pkgconf_init(void)
{
}

bool
muon_pkgconf_lookup(struct workspace *wk, uint32_t name, bool is_static, struct pkgconf_info *info)
{
	LOG_W("libpkgconf not enabled");
	return false;
}

bool
muon_pkgconf_get_variable(struct workspace *wk, const char *pkg_name, const char *var, uint32_t *res)
{
	LOG_W("libpkgconf not enabled");
	return false;
}
