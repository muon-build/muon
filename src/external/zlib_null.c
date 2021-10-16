#include "posix.h"

#include "external/zlib.h"
#include "log.h"

const bool have_zlib = false;

bool
muon_zlib_extract(uint8_t *data, uint64_t len, uint8_t **unzipped, uint64_t *unzipped_len)
{
	LOG_E("zlib not enabled");
	return false;
}
