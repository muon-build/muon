#include "posix.h"

#include <string.h>
#include <stdlib.h>

#include "archive.h"
#include "eval.h"
#include "external/sha-256.h"
#include "fetch.h"
#include "filesystem.h"
#include "inih.h"
#include "interpreter.h"
#include "log.h"
#include "mem.h"
#include "workspace.h"
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

struct wrap { const char *fields[wrap_fields_count]; };

static bool
wrap_parse_cb(void *_wrap, const char *path, const char *sect,
	const char *k, const char *v, uint32_t line)
{
	struct wrap *wrap = _wrap;

	if (!sect) {
		error_messagef(path, line, 1, "key not under [wrap-file] section");
		return false;
	} else if (strcmp(sect, "wrap-file") != 0) {
		error_messagef(path, line, 1, "key under invalid section \"%s\"", sect);
		return false;
	}

	uint32_t i;
	for (i = 0; i < wrap_fields_count; ++i) {
		if (strcmp(k, wrap_field_names[i]) == 0) {
			wrap->fields[i] = v;
			return true;
		}
	}

	error_messagef(path, line, 1, "invalid key \"%s\"", k);
	return false;
}

static bool
checksum(const uint8_t *file_buf, uint64_t len, const char *sha256)
{
	char buf[3] = { 0 };
	uint32_t i;
	uint8_t b, hash[32];

	if (strlen(sha256) != 64) {
		LOG_W(log_misc, "checksum '%s' is not 64 characters long", sha256);
		return false;
	}

	calc_sha_256(hash, file_buf, len);

	for (i = 0; i < 64; i += 2) {
		memcpy(buf, &sha256[i], 2);
		b = strtol(buf, NULL, 16);
		if (b != hash[i / 2]) {
			LOG_W(log_misc, "checksum mismatch");
			return false;
		}
	}

	return true;
}

static bool
fetch_checksum_extract(struct workspace *wk, uint32_t n_id,
	const char *src, const char *dest, const char *sha256,
	const char *dest_dir)
{
	uint8_t *dlbuf;
	uint64_t dlbuf_len;

	if (!fetch_fetch(src, &dlbuf, &dlbuf_len)) {
		return false;
	} else if (!checksum(dlbuf, dlbuf_len, sha256)) {
		z_free(dlbuf);
		return false;
	} else if (!archive_extract(dlbuf, dlbuf_len, dest_dir)) {
		z_free(dlbuf);
		return false;
	}

	z_free(dlbuf);
	return true;
}

bool
wrap_handle(struct workspace *wk, uint32_t n_id,
	const char *wrap_file, const char *dest_path)
{
	struct wrap wrap = { 0 };

	char *ini_buf;
	if (!ini_parse(wrap_file, &ini_buf, wrap_parse_cb, &wrap)) {
		goto err;
	}

	uint32_t i;
	for (i = 0; i < wrap_fields_count; ++i) {
		if (!wrap.fields[i]) {
			interp_error(wk, n_id, "wrap file at '%s' missing field '%s'", wrap_file, wrap_field_names[i]);
			goto err;
		}
	}

	fetch_init();

	if (!fetch_checksum_extract(wk, n_id, wrap.fields[wf_source_url],
		wrap.fields[wf_source_filename], wrap.fields[wf_source_hash], dest_path)) {
		goto err1;
	}

	if (!fetch_checksum_extract(wk, n_id, wrap.fields[wf_patch_url],
		wrap.fields[wf_patch_filename], wrap.fields[wf_patch_hash], dest_path)) {
		goto err1;
	}

	fetch_deinit();
	z_free(ini_buf);
	return true;
err1:
	fetch_deinit();
err:
	z_free(ini_buf);
	return false;
}
