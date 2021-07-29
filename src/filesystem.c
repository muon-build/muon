#include "posix.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "filesystem.h"
#include "log.h"
#include "mem.h"
#include "path.h"

bool
fs_stat(const char *path, struct stat *sb)
{
	if (stat(path, sb) != 0) {
		LOG_E("failed stat(%s): %s", path, strerror(errno));
		return false;
	}

	return true;
}

bool
fs_exists(const char *path)
{
	return access(path, F_OK) == 0;
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
fs_exe_exists(const char *path)
{
	struct stat sb;
	if (access(path, X_OK) != 0) {
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
		LOG_E("failed to create directory %s: %s", path, strerror(errno));
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
		if (buf[i] == PATH_SEP) {
			buf[i] = 0;
			if (!fs_dir_exists(buf)) {
				if (!fs_mkdir(buf)) {
					return false;
				}
			}
			buf[i] = PATH_SEP;
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
		LOG_E("failed to open '%s': %s", path, strerror(errno));
		return NULL;
	}

	return f;
}

bool
fs_fclose(FILE *file)
{
	if (fclose(file) != 0) {
		LOG_E("failed fclose: %s", strerror(errno));
		return false;
	}

	return true;
}

bool
fs_fsize(FILE *file, uint64_t *ret)
{
	int64_t size = 0;
	if (fseek(file, 0, SEEK_END) == -1) {
		LOG_E("failed fseek: %s", strerror(errno));
		return false;
	} else if ((size = ftell(file)) == -1) {
		LOG_E("failed ftell: %s", strerror(errno));
		return false;
	}

	rewind(file);

	assert(size >= 0);

	*ret = size;
	return true;
}

bool
fs_read_entire_file(const char *path, struct source *src)
{
	FILE *f;
	bool opened = false;
	size_t read;
	char *buf = NULL;

	*src = (struct source) { .label = path };

	if (strcmp(path, "-") == 0) {
		f = stdin;
	} else {
		if (!fs_file_exists(path)) {
			LOG_E("'%s' is not a file", path);
			return false;
		}

		if (!(f = fs_fopen(path, "r"))) {
			return false;
		}

		opened = true;
	}

	if (!fs_fsize(f, &src->len)) {
		if (opened) {
			fs_fclose(f);
		}

		return false;
	}

	buf = z_calloc(src->len + 1, 1);
	read = fread(buf, 1, src->len, f);

	if (opened) {
		if (!fs_fclose(f)) {
			z_free(buf);
			return false;
		}
	}

	if (read != src->len) {
		LOG_E("failed to read entire file, only read %ld/%ld bytes", read, (intmax_t)src->len);
		z_free(buf);
		return false;
	}

	src->src = buf;
	return true;
}

void
fs_source_destroy(struct source *src)
{
	if (src->src) {
		z_free((char *)src->src);
	}
}

bool
fs_fwrite(const void *ptr, size_t size, FILE *f)
{
	size_t r;
	int err;

	if (!size) {
		return true;
	}

	r = fwrite(ptr, 1, size, f);
	assert(r <= size);

	if (r == size) {
		return true;
	} else {
		if ((err = ferror(f))) {
			LOG_E("fwrite failed: %s", strerror(err));
		} else {
			LOG_E("fwrite failed: unknown");
		}
		return false;
	}
}

bool
fs_write(const char *path, const uint8_t *buf, uint64_t buf_len)
{
	FILE *f;
	if (!(f = fs_fopen(path, "w"))) {
		return false;
	}

	if (!fs_fwrite(buf, buf_len, f)) {
		LOG_E("failed to write entire file");
		fs_fclose(f);
		return false;
	}

	if (!fs_fclose(f)) {
		return false;
	}

	return true;
}

bool
fs_find_cmd(const char *cmd, const char **ret)
{
	uint32_t len;
	static char cmd_path[PATH_MAX];
	char path_elem[PATH_MAX];
	const char *env_path, *base_start;

	if (!path_is_basename(cmd)) {
		if (!path_make_absolute(cmd_path, PATH_MAX, cmd)) {
			return false;
		}

		if (fs_exe_exists(cmd_path)) {
			*ret = cmd_path;
			return true;
		} else {
			return false;
		}
	}

	if (!(env_path = getenv("PATH"))) {
		LOG_E("failed to get the value of PATH");
		return false;
	}

	base_start = env_path;
	while (true) {
		if (!*env_path || *env_path == ':') {
			len = env_path - base_start;
			assert(len + 1 < PATH_MAX);

			strncpy(path_elem, base_start, len);
			path_elem[len] = 0;
			base_start = env_path + 1;

			if (!path_join(cmd_path, PATH_MAX, path_elem, cmd)) {
				return false;
			}

			if (fs_exe_exists(cmd_path)) {
				*ret = cmd_path;
				return true;
			}

			if (!*env_path) {
				break;
			}
		}

		++env_path;
	}

	return false;
}


bool
fs_dup2(int oldfd, int newfd)
{
	if ((dup2(oldfd, newfd)) == -1) {
		LOG_E("failed dup2(%d, %d): %s", oldfd, newfd, strerror(errno));
		return false;
	}

	return true;
}

static bool
fs_fileno(FILE *f, int *ret)
{
	int v;

	if ((v = fileno(f)) == -1) {
		LOG_E("failed fileno: %s", strerror(errno));
		return false;
	}

	*ret = v;
	return true;
}

bool
fs_redirect(const char *path, const char *mode, int fd, int *old_fd)
{
	FILE *out;
	int out_fd;

	if ((*old_fd = dup(fd)) == -1) {
		LOG_E("failed dup(%d): %s", fd, strerror(errno));
		return false;
	} else if (!(out = fs_fopen(path, mode))) {
		return false;
	} else if (!fs_fileno(out, &out_fd)) {
		return false;
	} else if (!fs_dup2(out_fd, fd)) {
		return false;
	} else if (!fs_fclose(out)) {
		return false;
	}

	return true;
}

bool
fs_redirect_restore(int fd, int old_fd)
{
	if (!fs_dup2(old_fd, fd)) {
		return false;
	} else if (close(old_fd) == -1) {
		LOG_E("failed close(%d): %s", old_fd, strerror(errno));
		return false;
	}
	return true;
}
