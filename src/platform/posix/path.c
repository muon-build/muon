#include "posix.h"

#include <string.h>

#include "platform/path.h"

bool
path_is_absolute(const char *path)
{
	return *path == PATH_SEP;
}

bool
path_is_basename(const char *path)
{
	return strchr(path, PATH_SEP) == NULL;
}

