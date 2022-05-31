#include "posix.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "log.h"
#include "platform/filesystem.h"

bool
fs_mkdir(const char *path)
{
	if (mkdir(path, 0755) == -1) {
		LOG_E("failed to create directory %s: %s", path, strerror(errno));
		return false;
	}

	return true;
}
