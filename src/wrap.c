#include "posix.h"

#include <string.h>
#include <stdlib.h>

#include "error.h"
#include "external/curl.h"
#include "external/zlib.h"
#include "formats/ini.h"
#include "lang/eval.h"
#include "lang/interpreter.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "sha_256.h"
#include "wrap.h"

enum wrap_fields {
	wf_source_filename,
	wf_source_url,
	wf_source_hash,
	wf_patch_filename,
	wf_patch_url,
	wf_patch_hash,
	wrap_fields_count,
};

static const char *wrap_field_names[wrap_fields_count] = {
	[wf_source_filename] = "source_filename",
	[wf_source_url] = "source_url",
	[wf_source_hash] = "source_hash",
	[wf_patch_filename] = "patch_filename",
	[wf_patch_url] = "patch_url",
	[wf_patch_hash] = "patch_hash",
};

struct wrap {
	const char *fields[wrap_fields_count];
	struct source src;
};

static bool
wrap_parse_cb(void *_wrap, struct source *src, const char *sect,
	const char *k, const char *v, uint32_t line)
{
	struct wrap *wrap = _wrap;

	if (!sect) {
		error_messagef(src, line, 1, "key not under [wrap-file] section");
		return false;
	} else if (!k) {
		if (strcmp(sect, "wrap-file") != 0) {
			error_messagef(src, line, 1, "invalid section '%s'", sect);
			return false;
		}

		return true;
	}

	assert(k && v);

	uint32_t i;
	for (i = 0; i < wrap_fields_count; ++i) {
		if (strcmp(k, wrap_field_names[i]) == 0) {
			wrap->fields[i] = v;
			return true;
		}
	}

	error_messagef(src, line, 1, "invalid key \"%s\"", k);
	return false;
}

static bool
checksum(const uint8_t *file_buf, uint64_t len, const char *sha256)
{
	char buf[3] = { 0 };
	uint32_t i;
	uint8_t b, hash[32];

	if (strlen(sha256) != 64) {
		LOG_E("checksum '%s' is not 64 characters long", sha256);
		return false;
	}

	calc_sha_256(hash, file_buf, len);

	for (i = 0; i < 64; i += 2) {
		memcpy(buf, &sha256[i], 2);
		b = strtol(buf, NULL, 16);
		if (b != hash[i / 2]) {
			LOG_E("checksum mismatch");
			return false;
		}
	}

	return true;
}

static bool
fetch_checksum_extract(struct workspace *wk, const char *src, const char *dest, const char *sha256,
	const char *dest_dir)
{
	uint8_t *dlbuf;
	uint64_t dlbuf_len;

	if (!muon_curl_fetch(src, &dlbuf, &dlbuf_len)) {
		return false;
	} else if (!checksum(dlbuf, dlbuf_len, sha256)) {
		z_free(dlbuf);
		return false;
	} else if (!muon_zlib_extract(dlbuf, dlbuf_len, dest_dir)) {
		z_free(dlbuf);
		return false;
	}

	z_free(dlbuf);
	return true;
}

bool
wrap_handle(struct workspace *wk, const char *wrap_file, const char *dest_path)
{
	struct wrap wrap = { 0 };

	char *ini_buf;
	if (!ini_parse(wrap_file, &ini_buf, wrap_parse_cb, &wrap)) {
		goto err;
	}

	uint32_t i, end = wf_patch_filename;
	bool have_patch = wrap.fields[wf_patch_filename] != NULL;

	if (have_patch) {
		end = wrap_fields_count;
	}

	for (i = 0; i < end; ++i) {
		if (!wrap.fields[i]) {
			LOG_E("wrap file at '%s' missing field '%s'", wrap_file, wrap_field_names[i]);
			goto err;
		}
	}

	muon_curl_init();

	if (!fetch_checksum_extract(wk, wrap.fields[wf_source_url],
		wrap.fields[wf_source_filename], wrap.fields[wf_source_hash], dest_path)) {
		goto err1;
	}

	if (have_patch && !fetch_checksum_extract(wk, wrap.fields[wf_patch_url],
		wrap.fields[wf_patch_filename], wrap.fields[wf_patch_hash], dest_path)) {
		goto err1;
	}

	muon_curl_deinit();
	z_free(ini_buf);
	return true;
err1:
	muon_curl_deinit();
err:
	z_free(ini_buf);
	return false;
}
