#include "posix.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "filesystem.h"
#include "log.h"
#include "mem.h"

bool
fs_stat(const char *path, struct stat *sb)
{
	if (stat(path, sb) != 0) {
		LOG_W(log_misc, "failed stat(%s): %s", path, strerror(errno));
		return false;
	}

	return true;
}

bool
fs_file_exists(const char *path)
{
	struct stat sb;
	if (access(path, F_OK) != 0) {
		return false;
	} else if (!fs_stat(path, &sb)) {
		return false;
	} else if (!S_ISREG(sb.st_mode)) {
		return false;
	}

	return true;
}

bool
fs_dir_exists(const char *path)
{
	struct stat sb;
	if (access(path, F_OK) != 0) {
		return false;
	} else if (!fs_stat(path, &sb)) {
		return false;
	} else if (!S_ISDIR(sb.st_mode)) {
		return false;
	}

	return true;
}

bool
fs_mkdir(const char *path)
{
	if (mkdir(path, 0755) == -1) {
		LOG_W(log_misc, "failed to create directory %s: %s", path, strerror(errno));
		return false;
	}

	return true;
}

bool
fs_mkdir_p(const char *path)
{
	uint32_t i, len = strlen(path);
	char buf[PATH_MAX + 1] = { 0 };
	strncpy(buf, path, PATH_MAX);

	assert(len > 1);

	for (i = 1; i < len; ++i) {
		if (buf[i] == '/') {
			buf[i] = 0;
			if (!fs_dir_exists(buf)) {
				if (!fs_mkdir(buf)) {
					return false;
				}
			}
			buf[i] = '/';
		}
	}

	if (!fs_dir_exists(path)) {
		if (!fs_mkdir(path)) {
			return false;
		}
	}

	return true;
}

FILE *
fs_fopen(const char *path, const char *mode)
{
	FILE *f;
	if (!(f = fopen(path, mode))) {
		LOG_W(log_misc, "failed to open '%s': %s", path, strerror(errno));
		return NULL;
	}

	return f;
}

bool
fs_fclose(FILE *file)
{
	if (fclose(file) != 0) {
		LOG_W(log_misc, "failed fclose: %s", strerror(errno));
		return false;
	}

	return true;
}

bool
fs_fsize(FILE *file, uint64_t *ret)
{
	int64_t size;
	if (fseek(file, 0, SEEK_END) == -1) {
		LOG_W(log_misc, "failed fseek: %s", strerror(errno));
		return false;
	} else if ((size = ftell(file)) == -1) {
		LOG_W(log_misc, "failed ftell: %s", strerror(errno));
		return false;
	}

	rewind(file);

	assert(size >= 0);

	*ret = size;
	return true;
}

bool
fs_read_entire_file(const char *path, char **buf, uint64_t *size)
{
	FILE *f;
	size_t read;

	if (!fs_file_exists(path)) {
		LOG_W(log_misc, "'%s' is not a file", path);
		return false;
	}

	if (!(f = fs_fopen(path, "r"))) {
		return false;
	} else if (!fs_fsize(f, size)) {
		return false;
	}

	*buf = z_calloc(*size + 1, 1);
	read = fread(*buf, 1, *size, f);

	if (!fs_fclose(f)) {
		z_free(*buf);
		*buf = NULL;
		return false;
	} else if (read != *size) {
		LOG_W(log_misc, "failed to read entire file, only read %ld/%ld bytes", read, *size);
		z_free(*buf);
		*buf = NULL;
		return false;
	}

	return true;
}
