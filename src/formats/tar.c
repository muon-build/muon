#include "posix.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "formats/tar.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

enum tar_err {
	tar_err_ok,
	tar_err_badchksum,
	tar_err_nullrecord,
	tar_err_unknown_file_type,
};

enum tar_file_type {
	tar_file_type_reg,
	tar_file_type_lnk,
	tar_file_type_sym,
	tar_file_type_chr,
	tar_file_type_blk,
	tar_file_type_dir,
	tar_file_type_fifo,
};

struct tar_header {
	uint32_t mode;
	uint32_t owner;
	uint32_t size;
	uint32_t mtime;
	enum tar_file_type type;
	char name[100];
	char linkname[100];
	uint8_t *data;
};

struct tar {
	uint8_t *data;
	uint32_t len;
	uint32_t off;
};


struct tar_raw_header {
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
tar_round_up(uint32_t n, uint32_t incr)
{
	return n + (incr - n % incr) % incr;
}

static uint32_t
tar_checksum(const struct tar_raw_header* rh)
{
	uint32_t i;
	uint8_t *p = (uint8_t *)rh;
	uint32_t res = 256;
	for (i = 0; i < offsetof(struct tar_raw_header, checksum); i++) {
		res += p[i];
	}
	for (i = offsetof(struct tar_raw_header, type); i < sizeof(*rh); i++) {
		res += p[i];
	}
	return res;
}

static enum tar_err
tar_read(struct tar *tar, uint8_t *data, uint32_t size)
{
	/* L("requesting read of %d", size); */

	if (tar->off >= tar->len) {
		data[0] = 0;
		return tar_err_ok;
	}

	if (size + tar->off > tar->len) {
		size = tar->len - tar->off;
	}

	memcpy(data, &tar->data[tar->off], size);
	tar->off += size;

	return tar_err_ok;
}

static int
raw_to_header(struct tar_header *h, const struct tar_raw_header *rh)
{
	uint32_t chksum1 = 0, chksum2 = 0;

	/* If the checksum starts with a null byte we assume the record is NULL */
	if (!*rh->checksum) {
		return tar_err_nullrecord;
	}

	/* Build and compare checksum */
	chksum1 = tar_checksum(rh);
	chksum2 = strtol(rh->checksum, NULL, 8);
	if (chksum1 != chksum2) {
		return tar_err_badchksum;
	}

	/* Load raw header into header */
	h->mode = strtol(rh->mode, NULL, 8);
	h->owner = strtol(rh->owner, NULL, 8);
	h->size = strtol(rh->size, NULL, 8);
	h->mtime = strtol(rh->mtime, NULL, 8);

	switch (rh->type) {
	case '0': h->type = tar_file_type_reg;
		break;
	case '1': h->type = tar_file_type_lnk;
		break;
	case '2': h->type = tar_file_type_sym;
		break;
	case '3': h->type = tar_file_type_chr;
		break;
	case '4': h->type = tar_file_type_blk;
		break;
	case '5': h->type = tar_file_type_dir;
		break;
	case '6': h->type = tar_file_type_fifo;
		break;
	default: return tar_err_unknown_file_type;
	}

	strcpy(h->name, rh->name);
	strcpy(h->linkname, rh->linkname);

	return tar_err_ok;
}

static const char *
tar_strerror(enum tar_err err)
{
	switch (err) {
	case tar_err_ok: return "success";
	case tar_err_badchksum: return "bad checksum";
	case tar_err_nullrecord: return "null record";
	case tar_err_unknown_file_type: return "unknown file type";
	}
	return "unknown error";
}

static const char *
tar_file_type_to_s(enum tar_file_type type)
{
	switch (type) {
	case tar_file_type_reg: return "regular";
	case tar_file_type_lnk: return "link";
	case tar_file_type_sym: return "symlink";
	case tar_file_type_chr: return "character";
	case tar_file_type_blk: return "block";
	case tar_file_type_dir: return "directory";
	case tar_file_type_fifo: return "fifo";
	}

	return "unknown";
}

static enum tar_err
tar_read_header(struct tar *tar, struct tar_header *h)
{
	enum tar_err err;
	struct tar_raw_header rh;

	/* Read raw header */
	if ((err = tar_read(tar, (uint8_t *)&rh, sizeof(struct tar_raw_header)))) {
		return err;
	}

	/* Load raw header into header struct and return */
	if ((err = raw_to_header(h, &rh))) {
		return err;
	}

	h->data = &tar->data[tar->off];

	/* L("read header, %s, %d, %d", h->name, h->size, tar->off); */

	tar->off += tar_round_up(h->size, 512);
	return tar_err_ok;
}

bool
untar(uint8_t *data, uint64_t len, const char *destdir)
{
	struct tar tar = { .data = data, .len = len };
	struct tar_header hdr = { 0 };

	char path[PATH_MAX], dir[PATH_MAX];

	enum tar_err err;

	while ((err = tar_read_header(&tar, &hdr)) == tar_err_ok) {
		if (hdr.type == tar_file_type_dir) {
			continue;
		} else if (hdr.type != tar_file_type_reg) {
			LOG_E("skipping unsupported file '%s' of type '%s'", hdr.name, tar_file_type_to_s(hdr.type));
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

	if (err != tar_err_nullrecord) {
		LOG_E("problem unpacking tar: %s", tar_strerror(err));
		return false;
	}

	return true;
}
