#include "posix.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <zlib.h>

#include "archive.h"
#include "external/microtar.h"
#include "filesystem.h"
#include "log.h"
#include "mem.h"

#define CHUNK_SIZE 1048576ul

struct {
	uint8_t *data;
	uint32_t len;
	uint32_t off;
} mtar_stream_ctx;

static int
mtar_stream_read(mtar_t *tar, void *data, unsigned _size)
{
	/* L(log_misc, "requesting read of %d", _size); */

	if (mtar_stream_ctx.off >= mtar_stream_ctx.len) {
		((char *)data)[0] = 0;
		return 0;
	}

	uint32_t size;
	if (_size + mtar_stream_ctx.off > mtar_stream_ctx.len) {
		size = mtar_stream_ctx.len - mtar_stream_ctx.off;
	} else {
		size = _size;
	}

	memcpy(data, &mtar_stream_ctx.data[mtar_stream_ctx.off], size);
	mtar_stream_ctx.off += size;

	return MTAR_ESUCCESS;
}

static int
mtar_stream_seek(mtar_t *tar, unsigned pos)
{
	assert(pos < mtar_stream_ctx.len);
	mtar_stream_ctx.off = pos;

	return MTAR_ESUCCESS;
}

static int
mtar_stream_close(mtar_t *tar)
{
	return MTAR_ESUCCESS;
}

static const char *
mtar_type_to_s(char type)
{
	switch (type) {
	case MTAR_TREG: return "regular";
	case MTAR_TLNK: return "link";
	case MTAR_TSYM: return "symlink";
	case MTAR_TCHR: return "character";
	case MTAR_TBLK: return "block";
	case MTAR_TDIR: return "directory";
	case MTAR_TFIFO: return "fifo";
	}

	return "unknown";
}

static bool
archive_untar(uint8_t *data, uint64_t len, const char *destdir)
{
	mtar_stream_ctx.data = data;
	mtar_stream_ctx.len = len;
	mtar_stream_ctx.off = 0;

	mtar_t tar = {
		.read = mtar_stream_read,
		.seek = mtar_stream_seek,
		.close = mtar_stream_close,
	};
	mtar_header_t hdr = { 0 };

	uint8_t *buf = NULL;
	uint64_t buf_size = 0;
	char path[PATH_MAX + 1] = { 0 };

	while ((mtar_read_header(&tar, &hdr)) == MTAR_ESUCCESS) {
		if (hdr.type == MTAR_TDIR) {
			mtar_next(&tar);
			continue;
		} else if (hdr.type != MTAR_TREG) {
			LOG_W(log_misc, "skipping unsupported file '%s' of type '%s'", hdr.name, mtar_type_to_s(hdr.type));
		}

		if (hdr.size + 1 > buf_size) {
			buf_size = hdr.size + 1;
			buf = z_realloc(buf, buf_size);
		}

		memset(buf, 0, buf_size);
		mtar_read_data(&tar, buf, hdr.size);

		uint32_t len;
		int32_t i;
		len = snprintf(path, PATH_MAX, "%s/%s", destdir, hdr.name);
		for (i = len - 1; i >= 0; --i) {
			if (path[i] == '/') {
				path[i] = 0;

				if (!fs_mkdir_p(path)) {
					z_free(buf);
					return false;
				}
				path[i] = '/';
				break;
			}
		}

		if (!fs_write(path, buf, buf_size)) {
			z_free(buf);
			return false;
		}

		mtar_next(&tar);
	}

	z_free(buf);
	return true;
}

static const char *
archive_z_strerror(int err)
{
	switch (err) {
	case Z_OK: return "ok";
	case Z_STREAM_END: return "stream end";
	case Z_NEED_DICT: return "need dict";
	case Z_ERRNO: return "errno";
	case Z_STREAM_ERROR: return "stream";
	case Z_DATA_ERROR: return "data";
	case Z_MEM_ERROR: return "memory";
	case Z_BUF_ERROR: return "buffer";
	case Z_VERSION_ERROR: return "version";
	default: return "unknown";
	}
}

static bool
archive_unzip(uint8_t *data, uint64_t len, uint8_t **out, uint64_t *out_len)
{
	int err;
	uint8_t *buf = z_malloc(CHUNK_SIZE);
	uint64_t buf_len = CHUNK_SIZE;

	z_stream stream = {
		.next_in = data,
		.avail_in = len,

		.next_out = buf,
		.avail_out = CHUNK_SIZE
	};

	if ((err = inflateInit2(&stream, 16 + MAX_WBITS)) != Z_OK) {
		LOG_W(log_misc, "failed inflateInit2: %s", archive_z_strerror(err));
		return false;
	}

	while ((err = inflate(&stream, Z_SYNC_FLUSH)) == Z_OK) {
		buf_len += CHUNK_SIZE;
		buf = z_realloc(buf, buf_len);

		stream.next_out = &buf[buf_len - CHUNK_SIZE];
		stream.avail_out = CHUNK_SIZE;
	}
	*out_len = stream.total_out;

	if (err != Z_STREAM_END) {
		LOG_W(log_misc, "failed to inflate: %s", archive_z_strerror(err));
		if ((err = inflateEnd(&stream)) != Z_OK) {
			LOG_W(log_misc, "failed inflateEnd: %s", archive_z_strerror(err));
		}
		z_free(buf);
		return false;
	}

	if ((err = inflateEnd(&stream)) != Z_OK) {
		LOG_W(log_misc, "failed inflateEnd: %s", archive_z_strerror(err));
		z_free(buf);
		return false;
	}

	*out = buf;
	return true;
}

bool
archive_extract(uint8_t *data, uint64_t len, const char *destdir)
{
	uint8_t *unzipped;
	uint64_t unzipped_len;

	if (!archive_unzip(data, len, &unzipped, &unzipped_len)) {
		return false;
	} else if (!archive_untar(unzipped, unzipped_len, destdir)) {
		z_free(unzipped);
		return false;
	}

	z_free(unzipped);
	return true;
}
