#include "posix.h"

#include "external/samu.h"
#include "log.h"

const bool have_samu = false;

bool
muon_samu(uint32_t argc, char *const argv[])
{
	LOG_W(log_misc, "samu not avaliable");
	return false;
}
