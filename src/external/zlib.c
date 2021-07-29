#include "posix.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include <zlib.h>

#include "external/zlib.h"
#include "external/microtar.h"
#include "filesystem.h"
#include "log.h"
#include "mem.h"

const bool have_zlib = true;

#define CHUNK_SIZE 1048576ul

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
unzip(uint8_t *data, uint64_t len, uint8_t **out, uint64_t *out_len)
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
		LOG_E("failed inflateInit2: %s", archive_z_strerror(err));
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
		LOG_E("failed to inflate: %s", archive_z_strerror(err));
		if ((err = inflateEnd(&stream)) != Z_OK) {
			LOG_E("failed inflateEnd: %s", archive_z_strerror(err));
		}
		z_free(buf);
		return false;
	}

	if ((err = inflateEnd(&stream)) != Z_OK) {
		LOG_E("failed inflateEnd: %s", archive_z_strerror(err));
		z_free(buf);
		return false;
	}

	*out = buf;
	return true;
}

bool
muon_zlib_extract(uint8_t *data, uint64_t len, const char *destdir)
{
	uint8_t *unzipped;
	uint64_t unzipped_len;

	if (!unzip(data, len, &unzipped, &unzipped_len)) {
		return false;
	} else if (!untar(unzipped, unzipped_len, destdir)) {
		z_free(unzipped);
		return false;
	}

	z_free(unzipped);
	return true;
}
