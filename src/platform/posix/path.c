#include "posix.h"

#include "platform/path.h"

bool
path_is_absolute(const char *path)
{
	return *path == PATH_SEP;
}
