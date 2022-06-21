#include "posix.h"

#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buf_size.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

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

static bool
fs_lstat(const char *path, struct stat *sb)
{
	if (lstat(path, sb) != 0) {
		LOG_E("failed stat(%s): %s", path, strerror(errno));
		return false;
	}

	return true;
}

static bool
fs_lexists(const char *path)
{
	assert(path_is_absolute(path));

	return faccessat(0, path, F_OK, AT_SYMLINK_NOFOLLOW) == 0;
}

bool
fs_symlink_exists(const char *path)
{
	struct stat sb;
	if (!fs_lexists(path)) {
		return false;
	} else if (!fs_lstat(path, &sb)) {
		return false;
	} else if (!S_ISLNK(sb.st_mode)) {
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

static bool
fs_copy_link(const char *src, const char *dest)
{
	char link[PATH_MAX + 1] = { 0 };
	int n;
	if ((n = readlink(src, link, PATH_MAX)) == -1) {
		LOG_E("readlink('%s') failed: %s", src, strerror(errno));
		return false;
	} else if (n == PATH_MAX) {
		LOG_E("readlink got truncated value");
		return false;
	}

	return fs_make_symlink(link, dest, true);
}

bool
fs_copy_file(const char *src, const char *dest)
{
	bool res = false;
	FILE *f_src = NULL;
	int f_dest = 0;

	struct stat st;
	if (!fs_lstat(src, &st)) {
		goto ret;
	}

	if (S_ISLNK(st.st_mode)) {
		return fs_copy_link(src, dest);
	} else if (!S_ISREG(st.st_mode)) {
		LOG_E("unhandled file type");
		goto ret;
	}

	if (!(f_src = fs_fopen(src, "r"))) {
		goto ret;
	}

	if ((f_dest = open(dest, O_CREAT | O_WRONLY | O_TRUNC, st.st_mode)) == -1) {
		LOG_E("failed to create destination file %s: %s", dest, strerror(errno));
		goto ret;
	}

	assert(f_dest != 0);

	size_t r;
	ssize_t w;
	char buf[BUF_SIZE_32k];

	while ((r = fread(buf, 1, BUF_SIZE_32k, f_src)) > 0) {
		errno = 0; // to ensure that we get the error from write() only

		if ((w = write(f_dest, buf, r)) == -1) {
			LOG_E("failed write(): %s", strerror(errno));
			goto ret;
		} else {
			assert(w >= 0);

			if ((size_t)w < r) {
				LOG_E("incomplete write: %s", strerror(errno));
				goto ret;
			}
		}
	}

	if (!feof(f_src)) {
		LOG_E("incomplete read: %s", strerror(errno));
		goto ret;
	}

	res = true;
ret:
	if (f_src) {
		if (!fs_fclose(f_src)) {
			res = false;
		}
	}

	if (f_dest > 0) {
		if (close(f_dest) == -1) {
			LOG_E("failed close(): %s", strerror(errno));
			res = false;
		}
	}

	return res;
}

static bool
fs_remove(const char *path)
{
	if (remove(path) != 0) {
		LOG_E("failed remove(\"%s\"): %s", path, strerror(errno));
		return false;
	}

	return true;
}

bool
fs_make_symlink(const char *target, const char *path, bool force)
{
	if (force && fs_lexists(path)) {
		if (!fs_remove(path)) {
			return false;
		}
	}

	if (symlinkat(target, 0, path) != 0) {
		LOG_E("failed symlink(\"%s\", \"%s\"): %s", target, path, strerror(errno));
		return false;
	}

	return true;
}

const char *
fs_user_home(void)
{
	return getenv("HOME");
}

bool
fs_is_a_tty_from_fd(int fd)
{
	errno = 0;
	if (isatty(fd) == 1) {
		return true;
	} else {
		if (errno != ENOTTY) {
			LOG_W("isatty() failed: %s", strerror(errno));
		}
		return false;
	}
}

bool
fs_dir_foreach(const char *path, void *_ctx, fs_dir_foreach_cb cb)
{
	DIR *d;
	struct dirent *ent;

	if (!(d = opendir(path))) {
		LOG_E("failed opendir(%s): %s", path, strerror(errno));
		return false;
	}

	bool loop = true, res = true;
	while (loop && (ent = readdir(d))) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}

		switch (cb(_ctx, ent->d_name)) {
		case ir_cont:
			break;
		case ir_done:
			loop = false;
			break;
		case ir_err:
			loop = false;
			res = false;
			break;
		}
	}

	if (closedir(d) != 0) {
		LOG_E("failed closedir(): %s", strerror(errno));
		return false;
	}

	return res;
}
