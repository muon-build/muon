#include "posix.h"

#include <samu.h>

#include "external/samu.h"

bool
muon_samu(uint32_t argc, char *const argv[])
{
	return samu_main(argc, (char **)argv) == 0;
}
