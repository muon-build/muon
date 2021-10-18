#include "posix.h"

#include "external/libarchive.h"
#include "log.h"

const bool have_libarchive = false;

bool
muon_archive_extract(const char *buf, size_t size, const char *dest_path)
{
	LOG_E("archive support not enabled");
	return false;
}
