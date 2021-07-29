#include "posix.h"

#include "external/samu.h"
#include "log.h"

const bool have_samu = false;

bool
muon_samu(uint32_t argc, char *const argv[])
{
	LOG_E("samu not avaliable");
	return false;
}

bool
muon_samu_compdb(const char *build, const char *compile_commands)
{
	LOG_E("samu not avaliable");
	return false;
}
