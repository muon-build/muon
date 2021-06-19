#include "posix.h"

#include <samu.h>

#include "external/samu.h"

const bool have_samu = true;

bool
muon_samu(uint32_t argc, char *const argv[])
{
	return samu_main(argc, (char **)argv) == 0;
}
