#include "posix.h"

#include <stdio.h>

#include "filesystem.h"

bool
fs_file_exists(const char *path)
{
	FILE *f;
	if ((f = fopen(path, "r"))) {
		fclose(f);
		return true;
	}

	return false;
}
