#include "posix.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#ifdef BOSON_HAVE_ZLIB
#include <zlib.h>
#endif

#include "archive.h"
#include "external/microtar.h"
#include "filesystem.h"
#include "log.h"
#include "mem.h"

#define CHUNK_SIZE 1048576ul

static bool
archive_untar(uint8_t *data, uint64_t len, const char *destdir)
{
	struct mtar tar = { .data = data, .len = len };
	struct mtar_header hdr = { 0 };

	char path[PATH_MAX + 1] = { 0 };

	while ((mtar_read_header(&tar, &hdr)) == mtar_err_ok) {
		if (hdr.type == mtar_file_type_dir) {
			continue;
		} else if (hdr.type != mtar_file_type_reg) {
			LOG_W(log_misc, "skipping unsupported file '%s' of type '%s'", hdr.name, mtar_file_type_to_s(hdr.type));
			continue;
		}

		uint32_t len;
		int32_t i;
		len = snprintf(path, PATH_MAX, "%s/%s", destdir, hdr.name);
		for (i = len - 1; i >= 0; --i) {
			if (path[i] == '/') {
				path[i] = 0;

				if (!fs_mkdir_p(path)) {
					return false;
				}
				path[i] = '/';
				break;
			}
		}

		if (!fs_write(path, hdr.data, hdr.size)) {
			return false;
		}
	}

	return true;
}

static const char *
archive_z_strerror(int err)
{
	switch (err) {
#ifdef BOSON_HAVE_ZLIB
	case Z_OK: return "ok";
	case Z_STREAM_END: return "stream end";
	case Z_NEED_DICT: return "need dict";
	case Z_ERRNO: return "errno";
	case Z_STREAM_ERROR: return "stream";
	case Z_DATA_ERROR: return "data";
	case Z_MEM_ERROR: return "memory";
	case Z_BUF_ERROR: return "buffer";
	case Z_VERSION_ERROR: return "version";
#endif
	default: return "unknown";
	}
}

static bool
archive_unzip(uint8_t *data, uint64_t len, uint8_t **out, uint64_t *out_len)
{
#ifdef BOSON_HAVE_ZLIB
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
#else
	LOG_W(log_misc, "zlib not enabled");
	return false;
#endif
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
