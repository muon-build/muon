#include "posix.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "buf_size.h"
#include "lang/string.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"

static bool
fs_lstat(const char *path, struct stat *sb)
{
	if (lstat(path, sb) != 0) {
		LOG_E("failed lstat(%s): %s", path, strerror(errno));
		return false;
	}

	return true;
}

static bool
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
fs_symlink_exists(const char *path)
{
	struct stat sb;

	// use lstat here instead of fs_lstat because we want to ignore errors
	return lstat(path, &sb) == 0 && S_ISLNK(sb.st_mode);
}

static bool
fs_lexists(const char *path)
{
	return fs_exists(path) || fs_symlink_exists(path);
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
	bool res = false;
	uint32_t i, len = strlen(path);
	SBUF_manual(buf);
	path_copy(NULL, &buf, path);

	assert(len > 1);

	for (i = 1; i < len; ++i) {
		if (buf.buf[i] == PATH_SEP) {
			buf.buf[i] = 0;
			if (!fs_dir_exists(buf.buf)) {
				if (!fs_mkdir(buf.buf)) {
					goto ret;
				}
			}
			buf.buf[i] = PATH_SEP;
		}
	}

	if (!fs_dir_exists(path)) {
		if (!fs_mkdir(path)) {
			goto ret;
		}
	}

	res = true;
ret:
	sbuf_destroy(&buf);
	return res;
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
fs_fseek(FILE *file, size_t off)
{
	if (fseek(file, off, 0) == -1) {
		LOG_E("failed fseek: %s", strerror(errno));
		return false;
	}
	return true;
}

bool
fs_ftell(FILE *file, uint64_t *res)
{
	int64_t pos;
	if ((pos = ftell(file)) == -1) {
		LOG_E("failed ftell: %s", strerror(errno));
		return false;
	}

	assert(pos >= 0);
	*res = pos;
	return true;
}

bool
fs_fsize(FILE *file, uint64_t *ret)
{
	if (fseek(file, 0, SEEK_END) == -1) {
		LOG_E("failed fseek: %s", strerror(errno));
		return false;
	} else if (!fs_ftell(file, ret)) {
		return false;
	}

	rewind(file);
	return true;
}

static bool
fs_is_seekable(FILE *file, bool *res)
{
	int fd;
	if (!fs_fileno(file, &fd)) {
		return false;
	}

	errno = 0;
	if (lseek(fd, 0, SEEK_CUR) == -1) {
		if (errno == ESPIPE) {
			*res = false;
			return true;
		}
		LOG_E("lseek returned an unexpected error");
		return false;
	}

	*res = true;
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
			goto err;
		}

		if (!(f = fs_fopen(path, "r"))) {
			goto err;
		}

		opened = true;
	}

	/* If the file is seekable (i.e. not a pipe), then we can get the size
	 * and read it all at once.  Otherwise, read it in chunks.
	 */

	bool seekable;
	if (!fs_is_seekable(f, &seekable)) {
		goto err;
	}

	if (seekable) {
		if (!fs_fsize(f, &src->len)) {
			goto err;
		}

		buf = z_calloc(src->len + 1, 1);
		read = fread(buf, 1, src->len, f);

		if (read != src->len) {
			LOG_E("failed to read entire file, only read %zu/%" PRId64 "bytes", read, src->len);
			goto err;
		}
	} else {
		uint32_t buf_size = BUF_SIZE_4k;
		buf = z_calloc(buf_size + 1, 1);

		while ((read = fread(&buf[src->len], 1, BUF_SIZE_4k, f))) {
			src->len += read;

			if (src->len >= buf_size) {
				buf_size *= 2;
				buf = z_realloc(buf, buf_size);
				memset(&buf[src->len], 0, buf_size - src->len);
			}

		}

		assert(src->len < buf_size && buf[src->len] == 0);

		if (!feof(f)) {
			LOG_E("failed to read entire file, only read %" PRId64 "bytes", src->len);
			goto err;
		}
	}

	if (opened) {
		if (!fs_fclose(f)) {
			goto err;
		}
	}

	src->src = buf;
	return true;
err:
	if (opened) {
		fs_fclose(f);
	}

	if (buf) {
		z_free(buf);
	}
	return false;
}

void
fs_source_dup(const struct source *src, struct source *dup)
{
	uint32_t label_len = strlen(src->label);
	char *buf = z_calloc(src->len + label_len + 1, 1);
	dup->label = &buf[src->len];
	dup->src = buf;
	dup->len = src->len;

	memcpy(buf, src->src, src->len);
	memcpy(&buf[src->len], src->label, label_len);
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
fs_fread(void *ptr, size_t size, FILE *f)
{
	size_t r;
	int err;

	if (!size) {
		return true;
	}

	r = fread(ptr, 1, size, f);
	assert(r <= size);

	if (r == size) {
		return true;
	} else {
		if ((err = ferror(f))) {
			LOG_E("fread failed: %s", strerror(err));
		} else {
			LOG_E("fread failed: unknown");
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
fs_find_cmd(struct workspace *wk, struct sbuf *buf, const char *cmd)
{
	assert(*cmd);
	uint32_t len;
	const char *env_path, *base_start;

	sbuf_clear(buf);

	if (!path_is_basename(cmd)) {
		path_make_absolute(wk, buf, cmd);

		if (fs_exe_exists(buf->buf)) {
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

			sbuf_clear(buf);
			sbuf_pushn(wk, buf, base_start, len);

			base_start = env_path + 1;

			path_push(wk, buf, cmd);

			if (fs_exe_exists(buf->buf)) {
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
fs_has_cmd(const char *cmd)
{
	SBUF_manual(buf);
	bool res = fs_find_cmd(NULL, &buf, cmd);
	sbuf_destroy(&buf);
	return res;
}

static bool
fs_dup2(int oldfd, int newfd)
{
	if ((dup2(oldfd, newfd)) == -1) {
		LOG_E("failed dup2(%d, %d): %s", oldfd, newfd, strerror(errno));
		return false;
	}

	return true;
}

bool
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

static bool
fs_copy_link(const char *src, const char *dest)
{
	bool res = false;
	SBUF_manual(buf);
	ssize_t n;
	while ((n = readlink(src, buf.buf, buf.cap)) != -1 && (uint32_t)n >= buf.cap) {
		sbuf_grow(NULL, &buf, buf.cap);
	}

	if (n == -1) {
		LOG_E("readlink('%s') failed: %s", src, strerror(errno));
		goto ret;
	}

	buf.buf[n] = '\0';
	res = fs_make_symlink(buf.buf, dest, true);
ret:
	sbuf_destroy(&buf);
	return res;
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

struct fs_copy_dir_ctx {
	const char *src_base, *dest_base;
};

static enum iteration_result
fs_copy_dir_iter(void *_ctx, const char *path)
{
	enum iteration_result res = ir_err;
	struct fs_copy_dir_ctx *ctx = _ctx;
	struct stat sb;
	SBUF_manual(src);
	SBUF_manual(dest);

	path_join(NULL, &src, ctx->src_base, path);
	path_join(NULL, &dest, ctx->dest_base, path);

	if (!fs_stat(src.buf, &sb)) {
		goto ret;
	}

	if (S_ISDIR(sb.st_mode)) {
		if (!fs_dir_exists(dest.buf)) {
			if (!fs_mkdir(dest.buf)) {
				goto ret;
			}
		}

		if (!fs_copy_dir(src.buf, dest.buf)) {
			goto ret;
		}
	} else if (S_ISREG(sb.st_mode)) {
		if (!fs_copy_file(src.buf, dest.buf)) {
			goto ret;
		}
	} else {
		LOG_E("unhandled file type '%s'", path);
		goto ret;
	}

	res = ir_cont;
ret:
	sbuf_destroy(&src);
	sbuf_destroy(&dest);
	return res;
}

bool
fs_copy_dir(const char *src_base, const char *dest_base)
{
	struct fs_copy_dir_ctx ctx = {
		.src_base = src_base,
		.dest_base = dest_base,
	};

	if (!fs_dir_exists(dest_base)) {
		if (!fs_mkdir(dest_base)) {
			return ir_err;
		}
	}

	return fs_dir_foreach(src_base, &ctx, fs_copy_dir_iter);
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
fs_is_a_tty(FILE *f)
{
	int fd;
	if (!fs_fileno(f, &fd)) {
		return false;
	}
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
fs_chmod(const char *path, uint32_t mode)
{
	if (mode & S_ISVTX) {
		struct stat sb;
		if (!fs_stat(path, &sb)) {
			return false;
		}
		if (!S_ISDIR(sb.st_mode)) {
			LOG_E("attempt to set sticky bit on regular file: %s", path);
			return false;
		}
	}

	if (fs_symlink_exists(path)) {
		if (fchmodat(AT_FDCWD, path, (mode_t)mode, AT_SYMLINK_NOFOLLOW) == -1) {
			if (errno == EOPNOTSUPP) {
				LOG_W("changing permissions of symlinks not supported");
				return true;
			}

			LOG_E("failed fchmodat(AT_FCWD, %s, %o, AT_SYMLINK_NOFOLLOW): %s", path, mode, strerror(errno));
			return false;
		}
	} else if (chmod(path, (mode_t)mode) == -1) {
		LOG_E("failed chmod(%s, %o): %s", path, mode, strerror(errno));
		return false;
	}

	return true;
}

bool
fs_copy_metadata(const char *src, const char *dest)
{
	struct stat sb;
	if (!fs_stat(src, &sb)) {
		return false;
	}

	if (!fs_chmod(dest, sb.st_mode)) {
		return false;
	}

	return true;
}
