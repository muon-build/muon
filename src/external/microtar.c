/*
 * Copyright (c) 2017 rxi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "posix.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "external/microtar.h"
#include "filesystem.h"
#include "log.h"
#include "path.h"

struct mtar_raw_header {
	char name[100];
	char mode[8];
	char owner[8];
	char group[8];
	char size[12];
	char mtime[12];
	char checksum[8];
	char type;
	char linkname[100];
	char _padding[255];
};

static uint32_t
mtar_round_up(uint32_t n, uint32_t incr)
{
	return n + (incr - n % incr) % incr;
}


static uint32_t
mtar_checksum(const struct mtar_raw_header* rh)
{
	uint32_t i;
	uint8_t *p = (uint8_t *)rh;
	uint32_t res = 256;
	for (i = 0; i < offsetof(struct mtar_raw_header, checksum); i++) {
		res += p[i];
	}
	for (i = offsetof(struct mtar_raw_header, type); i < sizeof(*rh); i++) {
		res += p[i];
	}
	return res;
}

static enum mtar_err
mtar_read(struct mtar *tar, uint8_t *data, uint32_t size)
{
	/* L(log_misc, "requesting read of %d", size); */

	if (tar->off >= tar->len) {
		data[0] = 0;
		return mtar_err_ok;
	}

	if (size + tar->off > tar->len) {
		size = tar->len - tar->off;
	}

	memcpy(data, &tar->data[tar->off], size);
	tar->off += size;

	return mtar_err_ok;
}

static int
raw_to_header(struct mtar_header *h, const struct mtar_raw_header *rh)
{
	uint32_t chksum1 = 0, chksum2 = 0;

	/* If the checksum starts with a null byte we assume the record is NULL */
	if (!*rh->checksum) {
		return mtar_err_nullrecord;
	}

	/* Build and compare checksum */
	chksum1 = mtar_checksum(rh);
	chksum2 = strtol(rh->checksum, NULL, 8);
	if (chksum1 != chksum2) {
		return mtar_err_badchksum;
	}

	/* Load raw header into header */
	h->mode = strtol(rh->mode, NULL, 8);
	h->owner = strtol(rh->owner, NULL, 8);
	h->size = strtol(rh->size, NULL, 8);
	h->mtime = strtol(rh->mtime, NULL, 8);

	switch (rh->type) {
	case '0': h->type = mtar_file_type_reg;
		break;
	case '1': h->type = mtar_file_type_lnk;
		break;
	case '2': h->type = mtar_file_type_sym;
		break;
	case '3': h->type = mtar_file_type_chr;
		break;
	case '4': h->type = mtar_file_type_blk;
		break;
	case '5': h->type = mtar_file_type_dir;
		break;
	case '6': h->type = mtar_file_type_fifo;
		break;
	default: return mtar_err_unknown_file_type;
	}

	strcpy(h->name, rh->name);
	strcpy(h->linkname, rh->linkname);

	return mtar_err_ok;
}

const char *
mtar_strerror(enum mtar_err err)
{
	switch (err) {
	case mtar_err_ok: return "success";
	case mtar_err_badchksum: return "bad checksum";
	case mtar_err_nullrecord: return "null record";
	case mtar_err_unknown_file_type: return "unknown file type";
	}
	return "unknown error";
}

const char *
mtar_file_type_to_s(enum mtar_file_type type)
{
	switch (type) {
	case mtar_file_type_reg: return "regular";
	case mtar_file_type_lnk: return "link";
	case mtar_file_type_sym: return "symlink";
	case mtar_file_type_chr: return "character";
	case mtar_file_type_blk: return "block";
	case mtar_file_type_dir: return "directory";
	case mtar_file_type_fifo: return "fifo";
	}

	return "unknown";
}

enum mtar_err
mtar_read_header(struct mtar *tar, struct mtar_header *h)
{
	enum mtar_err err;
	struct mtar_raw_header rh;

	/* Read raw header */
	if ((err = mtar_read(tar, (uint8_t *)&rh, sizeof(struct mtar_raw_header)))) {
		return err;
	}

	/* Load raw header into header struct and return */
	if ((err = raw_to_header(h, &rh))) {
		return err;
	}

	h->data = &tar->data[tar->off];

	/* L(log_misc, "read header, %s, %d, %d", h->name, h->size, tar->off); */

	tar->off += mtar_round_up(h->size, 512);
	return mtar_err_ok;
}

bool
untar(uint8_t *data, uint64_t len, const char *destdir)
{
	struct mtar tar = { .data = data, .len = len };
	struct mtar_header hdr = { 0 };

	char path[PATH_MAX], dir[PATH_MAX];

	while ((mtar_read_header(&tar, &hdr)) == mtar_err_ok) {
		if (hdr.type == mtar_file_type_dir) {
			continue;
		} else if (hdr.type != mtar_file_type_reg) {
			LOG_W(log_misc, "skipping unsupported file '%s' of type '%s'", hdr.name, mtar_file_type_to_s(hdr.type));
			continue;
		}

		if (!path_join(path, PATH_MAX, destdir, hdr.name)) {
			return false;
		} else if (!path_dirname(dir, PATH_MAX, path)) {
			return false;
		} else if (!fs_mkdir_p(dir)) {
			return false;
		} else if (!fs_write(path, hdr.data, hdr.size)) {
			return false;
		}
	}

	return true;
}
