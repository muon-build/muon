#include "posix.h"

#include "external/zlib.h"
#include "log.h"

bool
muon_zlib_extract(uint8_t *data, uint64_t len, const char *destdir)
{
	LOG_W(log_misc, "zlib not enabled");
	return false;
}
