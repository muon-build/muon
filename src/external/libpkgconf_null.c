#include "posix.h"

#include "external/pkgconf.h"
#include "log.h"

void
muon_pkgconf_deinit(void)
{
}

void
muon_pkgconf_init(void)
{
}

bool
muon_pkgconf_lookup(struct workspace *wk, const char *name, struct pkgconf_info *info)
{
	LOG_W(log_misc, "pkgconf not enabled");
	return false;
}

bool
muon_pkgconf_get_variable(struct workspace *wk, const char *pkg_name, char *var, uint32_t *res)
{
	LOG_W(log_misc, "pkgconf not enabled");
	return false;
}
