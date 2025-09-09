/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <errno.h>
#include <string.h>

#include "buf_size.h"
#include "error.h"
#include "lang/string.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/os.h"
#include <sys/param.h>
#include "platform/path.h"

static struct {
	char cwd[4096];
} path_ctx;

// These functions are defined in platform/<plat>/os.c
//
// They should only be called indirectly through path_chdir and path_cwd though
// so the prototypes were removed from os.h.
bool os_chdir(const char *path);
char *os_getcwd(char *buf, size_t size);

static void
path_getcwd(struct workspace *wk)
{
	if (!os_getcwd(path_ctx.cwd, sizeof(path_ctx.cwd))) {
		error_unrecoverable("getcwd failed: %s", strerror(errno));
	}
}

void
path_init(struct workspace *wk)
{
	path_getcwd(wk);
}

void
_path_normalize(struct workspace *wk, struct tstr *buf, bool optimize)
{
	uint32_t parents = 0;
	char *part, *sep;
	uint32_t part_len, i, slen = buf->len, blen = 0, sep_len;
	bool loop = true, skip_part;

	if (!buf->len) {
		return;
	}

	path_to_posix(buf->buf);
	part = buf->buf;

	if (*part == PATH_SEP) {
		++part;
		--slen;
		++blen;
	}

	while (*part && loop) {
		if (!(sep = strchr(part, PATH_SEP))) {
			sep = &part[strlen(part)];
			loop = false;
		}

		sep_len = *sep ? 1 : 0;
		part_len = sep - part;
		skip_part = false;

		if (!part_len || (part_len == 1 && *part == '.')) {
			// eliminate empty elements (a//b -> a/b) and
			// current-dir elements (a/./b -> a/b)
			skip_part = true;
		} else if (optimize && part_len == 2 && part[0] == '.' && part[1] == '.') {
			// convert something like a/../b into b
			if (parents) {
				for (i = (part - 2) - buf->buf; i > 0; --i) {
					if (buf->buf[i] == PATH_SEP) {
						break;
					}
				}

				if (buf->buf[i] == PATH_SEP) {
					++i;
				}

				part = &buf->buf[i];
				skip_part = true;
				blen -= (sep - &buf->buf[i]) - part_len;
				--parents;
			} else {
				part = sep + sep_len;
			}
		} else {
			++parents;
			part = sep + sep_len;
		}

		if (skip_part) {
			memmove(part, sep + sep_len, slen);
		} else {
			blen += part_len + sep_len;
		}

		slen -= part_len + sep_len;
	}

	if (!blen) {
		buf->buf[0] = '.';
		buf->len = 1;
	} else if (blen > 1 && buf->buf[blen - 1] == PATH_SEP) {
		buf->buf[blen - 1] = 0;
		buf->len = blen - 1;
	} else {
		buf->len = blen;
	}
}

void
path_copy(struct workspace *wk, struct tstr *sb, const char *path)
{
	tstr_clear(sb);
	tstr_pushs(wk, sb, path);
	_path_normalize(wk, sb, false);
}

bool
path_chdir(struct workspace *wk, const char *path)
{
	if (!os_chdir(path)) {
		LOG_E("failed chdir(%s): %s", path, strerror(errno));
		return false;
	}

	path_getcwd(wk);
	return true;
}

void
path_copy_cwd(struct workspace *wk, struct tstr *sb)
{
	path_copy(wk, sb, path_ctx.cwd);
}

const char *
path_cwd(void)
{
	return path_ctx.cwd;
}

void
path_join_absolute(struct workspace *wk, struct tstr *sb, const char *a, const char *b)
{
	path_copy(wk, sb, a);
	tstr_push(wk, sb, PATH_SEP);
	tstr_pushs(wk, sb, b);
	_path_normalize(wk, sb, false);
}

void
path_push(struct workspace *wk, struct tstr *sb, const char *b)
{
	if (!*b) {
		/* Special-case path_1 / '' to mean "append a / to the current
		 * path".
		 */
		_path_normalize(wk, sb, false);
		tstr_push(wk, sb, PATH_SEP);
		return;
	}

	uint32_t b_len = strlen(b);

	if (path_is_absolute(b) || !sb->len) {
		path_copy(wk, sb, b);
	} else {
		tstr_push(wk, sb, PATH_SEP);
		tstr_pushn(wk, sb, b, b_len);
		_path_normalize(wk, sb, false);
	}

	if (sb->buf[sb->len - 1] != PATH_SEP && b[b_len - 1] == PATH_SEP) {
		tstr_push(wk, sb, PATH_SEP);
	}
}

void
path_join(struct workspace *wk, struct tstr *sb, const char *a, const char *b)
{
	tstr_clear(sb);
	path_push(wk, sb, a);
	path_push(wk, sb, b);
}

void
path_make_absolute(struct workspace *wk, struct tstr *buf, const char *path)
{
	if (path_is_absolute(path)) {
		path_copy(wk, buf, path);
	} else {
		path_join(wk, buf, path_ctx.cwd, path);
	}
}

void
path_relative_to(struct workspace *wk, struct tstr *buf, const char *base_raw, const char *path_raw)
{
	/*
	 * input: base="/path/to/build/"
	 *        path="/path/to/build/tgt/dir/libfoo.a"
	 * output: "tgt/dir/libfoo.a"
	 *
	 * input: base="/path/to/build"
	 *        path="/path/to/build/libfoo.a"
	 * output: "libfoo.a"
	 *
	 * input: base="/path/to/build"
	 *        path="/path/to/src/asd.c"
	 * output: "../src/asd.c"
	 */

	tstr_clear(buf);

	TSTR(tmp1);
	tstr_pushs(wk, &tmp1, base_raw);
	_path_normalize(wk, &tmp1, true);

	TSTR(tmp2);
	tstr_pushs(wk, &tmp2, path_raw);
	_path_normalize(wk, &tmp2, true);

	const char *base = tmp1.buf, *path = tmp2.buf;

	if (!path_is_absolute(base)) {
		LOG_E("base path '%s' is not absolute", base);
		assert(false);
	} else if (!path_is_absolute(path)) {
		LOG_E("path '%s' is not absolute", path);
		assert(false);
	}

	uint32_t i = 0, common_end = 0;

	if (strcmp(base, path) == 0) {
		tstr_push(wk, buf, '.');
		return;
	}

	while (base[i] && path[i] && base[i] == path[i]) {
		if (base[i] == PATH_SEP) {
			common_end = i;
		}

		++i;
	}

	if ((!base[i] && path[i] == PATH_SEP)) {
		common_end = i;
	} else if (!path[i] && base[i] == PATH_SEP) {
		common_end = i;
	}

	if (i <= 1) {
		/* -> base and path match only at root, so take path */
		path_copy(wk, buf, path);
		return;
	}

	if (base[common_end] && base[common_end + 1]) {
		bool have_part = true;
		i = common_end + 1;
		do {
			if (have_part) {
				tstr_pushs(wk, buf, "..");
				tstr_push(wk, buf, PATH_SEP);
				have_part = false;
			}

			if (base[i] == PATH_SEP) {
				have_part = true;
			}
			++i;
		} while (base[i]);
	}

	if (path[common_end]) {
		tstr_pushs(wk, buf, &path[common_end + 1]);
	}

	_path_normalize(wk, buf, false);
}

void
path_without_ext(struct workspace *wk, struct tstr *buf, const char *path)
{
	int32_t i;

	tstr_clear(buf);

	if (!*path) {
		return;
	}

	bool have_ext = false;

	TSTR(tmp);
	path_copy(wk, &tmp, path);
	path = tmp.buf;
	for (i = strlen(path) - 1; i >= 0; --i) {
		if (path[i] == '.') {
			have_ext = true;
			break;
		} else if (path[i] == PATH_SEP) {
			break;
		}
	}

	if (have_ext) {
		tstr_pushn(wk, buf, path, i);
	} else {
		path_copy(wk, buf, path);
	}
	_path_normalize(wk, buf, false);
}

void
path_basename(struct workspace *wk, struct tstr *buf, const char *path)
{
	int32_t i;

	tstr_clear(buf);

	if (!*path) {
		return;
	}

	TSTR(tmp);
	path_copy(wk, &tmp, path);
	path = tmp.buf;
	for (i = strlen(path) - 1; i >= 0; --i) {
		if (path[i] == PATH_SEP) {
			++i;
			break;
		}
	}

	if (i < 0) {
		i = 0;
	}

	tstr_pushs(wk, buf, &path[i]);
	_path_normalize(wk, buf, false);
}

void
path_dirname(struct workspace *wk, struct tstr *buf, const char *path)
{
	int32_t i;

	tstr_clear(buf);

	if (!*path) {
		goto return_dot;
	}

	TSTR(tmp);
	path_copy(wk, &tmp, path);
	path = tmp.buf;
	for (i = strlen(path) - 1; i >= 0; --i) {
		if (path[i] == PATH_SEP) {
			if (i == 0) {
				/* make dirname of '/path' be '/', not '' */
				tstr_pushn(wk, buf, path, 1);
			} else {
				tstr_pushn(wk, buf, path, i);
			}

			_path_normalize(wk, buf, false);
			return;
		}
	}

return_dot:
	tstr_pushs(wk, buf, ".");
}

bool
path_is_subpath(struct workspace *wk, const char *base, const char *sub)
{
	if (!*base) {
		return false;
	}

	TSTR(base_tmp);
	TSTR(sub_tmp);
	path_copy(wk, &base_tmp, base);
	base = base_tmp.buf;
	path_copy(wk, &sub_tmp, sub);
	sub = sub_tmp.buf;

	uint32_t i = 0;
	while (true) {
		if (!base[i]) {
			assert(i);
			if (sub[i] == PATH_SEP || sub[i - 1] == PATH_SEP) {
				return true;
			}
		}

		if (base[i] == sub[i]) {
			if (!base[i]) {
				return true;
			}
		} else {
			return false;
		}

		assert(base[i] && sub[i]);
		++i;
	}
}

void
path_executable(struct workspace *wk, struct tstr *buf, const char *path)
{
	if (path_is_basename(path)) {
		tstr_clear(buf);
		tstr_push(wk, buf, '.');
		tstr_push(wk, buf, PATH_SEP);
		tstr_pushs(wk, buf, path);
	} else {
		path_copy(wk, buf, path);
	}
}

#define path_begins_with_win32_drive_impl(path, p) \
	if (!path[0] || !path[1] || !path[2]) { \
		return false; \
	} \
	\
	/* c:/ or c:\ case insensitive */ \
	return (((path[0] >= p## 'a') && (path[0] <= p## 'z')) || ((path[0] >= p## 'A') && (path[0] <= p## 'Z'))) && (path[1] == p## ':') \
	       && ((path[2] == p## '/') || (path[2] == p## '\\'));

bool
path_begins_with_win32_drive(const char *path)
{
	path_begins_with_win32_drive_impl(path, );
}

#ifdef _WIN32
bool
path_wide_begins_with_win32_drive(const wchar_t *path)
{
	path_begins_with_win32_drive_impl(path, L);
}
#endif
