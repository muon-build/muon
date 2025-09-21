/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-FileCopyrightText: Eli Schwartz <eschwartz@archlinux.org>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#ifdef __sun
/* for symlinkat() and fchmodat(), as _POSIX_C_SOURCE does not enable them
 * (yet)
 */
#define __EXTENSIONS__
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "buf_size.h"
#include "lang/string.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/os.h"
#include "platform/path.h"

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
fs_mkdir_p(struct workspace *wk, const char *path)
{
	uint32_t i, len = strlen(path);
	TSTR(buf);
	path_copy(wk, &buf, path);

	assert(len >= 1);

	i = 0;
	if (path_is_absolute(buf.buf)) {
		char *p;
		if ((p = strchr(buf.buf, PATH_SEP))) {
			i = (p - buf.buf) + 1;
		}
	}

	for (; i < len; ++i) {
		if (buf.buf[i] == PATH_SEP) {
			buf.buf[i] = 0;
			if (!fs_mkdir(buf.buf, true)) {
				return false;
			}
			buf.buf[i] = PATH_SEP;
		}
	}

	if (!fs_mkdir(path, true)) {
		return false;
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

	*src = (struct source){ .label = path, .type = source_type_file };

	/* If the file is seekable (i.e. not a pipe), then we can get the size
	 * and read it all at once.  Otherwise, read it in chunks.
	 */

	bool seekable = true;

	if (strcmp(path, "-") == 0) {
		seekable = false;
		f = stdin;
	} else {
		if (!fs_file_exists(path)) {
			LOG_E("'%s' is not a file", path);
			goto err;
		}

		if (!(f = fs_fopen(path, "rb"))) {
			goto err;
		}

		opened = true;
	}

	if (seekable) {
		if (!fs_is_seekable(f, &seekable)) {
			goto err;
		}
	}

	if (seekable) {
		if (!fs_fsize(f, &src->len)) {
			goto err;
		}

		buf = z_calloc(src->len + 1, 1);
		read = fread(buf, 1, src->len, f);

		if (read != src->len) {
			LOG_E("failed to read entire file, only read %" PRIu64 "/%" PRId64 "bytes",
				(uint64_t)read,
				src->len);
			goto err;
		}
	} else {
		uint32_t buf_size = BUF_SIZE_4k;
		buf = z_calloc(buf_size + 1, 1);

		while ((read = fread(&buf[src->len], 1, buf_size - src->len, f))) {
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
	dup->type = src->type;

	memcpy(buf, src->src, src->len);
	memcpy(&buf[src->len], src->label, label_len);
}

void
fs_source_destroy(struct source *src)
{
	if (!src->is_weak_reference) {
		if (src->src) {
			z_free((char *)src->src);
		}
	}
	src->src = 0;
	src->len = 0;
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
		if (feof(f)) {
			LOG_E("fread got EOF");
		} else if ((err = ferror(f))) {
			LOG_E("fread failed: %s", strerror(err));
		} else {
			LOG_E("fread failed: unknown");
		}
		return false;
	}
}

int32_t
fs_read(int fd, void *buf, uint32_t buf_len)
{
	int32_t res = read(fd, buf, buf_len);
	if (res < 0) {
		LOG_E("read: %s", strerror(errno));
	}

	return res;
}

bool
fs_write(const char *path, const uint8_t *buf, uint64_t buf_len)
{
	FILE *f;
	if (!(f = fs_fopen(path, "wb"))) {
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
fs_make_writeable_if_exists(const char *path)
{
	struct stat sb;
	if (stat(path, &sb) == 0) {
		if (!(sb.st_mode & S_IWUSR)) {
			if (!fs_chmod(path, sb.st_mode | S_IWUSR)) {
				return false;
			}
		}
	}

	return true;
}

enum iteration_result
fs_copy_dir_iter(void *_ctx, const char *path)
{
	struct fs_copy_dir_ctx *ctx = _ctx;
	struct stat sb;
	TSTR(src);
	TSTR(dest);

	path_join(ctx->wk, &src, ctx->src_base, path);
	path_join(ctx->wk, &dest, ctx->dest_base, path);

	if (!fs_stat(src.buf, &sb)) {
		return ir_err;
	}

	if (S_ISDIR(sb.st_mode)) {
		if (!fs_mkdir(dest.buf, ctx->force)) {
			return ir_err;
		}

		struct fs_copy_dir_ctx sub_ctx = *ctx;
		sub_ctx.src_base = src.buf;
		sub_ctx.dest_base = dest.buf;
		if (!fs_copy_dir_ctx(ctx->wk, &sub_ctx)) {
			return ir_err;
		}
	} else if (S_ISREG(sb.st_mode)) {
		if (ctx->file_cb) {
			ctx->file_cb(ctx->usr_ctx, src.buf, dest.buf);
		}

		if (!fs_copy_file(src.buf, dest.buf, ctx->force)) {
			return ir_err;
		}
	} else {
		LOG_E("unhandled file type '%s'", path);
		return ir_err;
	}

	return ir_cont;
}

bool
fs_copy_dir_ctx(struct workspace *wk, struct fs_copy_dir_ctx *ctx)
{
	ctx->wk = wk;

	if (!fs_mkdir(ctx->dest_base, true)) {
		return ir_err;
	}

	return fs_dir_foreach(ctx->src_base, ctx, fs_copy_dir_iter);
}

bool
fs_copy_dir(struct workspace *wk, const char *src_base, const char *dest_base, bool force)
{
	struct fs_copy_dir_ctx ctx = {
		.src_base = src_base,
		.dest_base = dest_base,
		.force = force,
	};

	return fs_copy_dir_ctx(wk, &ctx);
}

struct fs_rmdir_ctx {
	struct workspace *wk;
	const char *base_dir;
	bool force;
};

static enum iteration_result
fs_rmdir_iter(void *_ctx, const char *path)
{
	struct fs_rmdir_ctx *ctx = _ctx;
	struct stat sb;

	TSTR(name);

	path_join(ctx->wk, &name, ctx->base_dir, path);

	if (fs_symlink_exists(name.buf)) {
		if (!fs_remove(name.buf)) {
			return ir_err;
		}
		return ir_cont;
	} else if (stat(name.buf, &sb) != 0) {
		if (ctx->force) {
			return ir_cont;
		} else {
			LOG_E("failed stat(%s): %s", path, strerror(errno));
			return ir_err;
		}
	}

	if (S_ISDIR(sb.st_mode)) {
		if (!fs_rmdir_recursive(ctx->wk, name.buf, ctx->force)) {
			return ir_err;
		}

		if (!fs_rmdir(name.buf, ctx->force)) {
			return ir_err;
		}

	} else if (S_ISREG(sb.st_mode)) {
		if (!fs_remove(name.buf)) {
			return ir_err;
		}

	} else {
		LOG_E("unhandled file type: %s", name.buf);
		return ir_err;
	}

	return ir_cont;
}

bool
fs_rmdir_recursive(struct workspace *wk, const char *path, bool force)
{
	struct fs_rmdir_ctx ctx = {
		.wk = wk,
		.base_dir = path,
		.force = force,
	};

	return fs_dir_foreach(path, &ctx, fs_rmdir_iter);
}

bool
fs_is_a_tty(FILE *f)
{
	int fd;
	if (!fs_fileno(f, &fd)) {
		return false;
	}
	return fs_is_a_tty_from_fd(fd);
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
