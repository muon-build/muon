#include "posix.h"

#include <string.h>
#include <stdlib.h>

#include "error.h"
#include "external/libarchive.h"
#include "external/libcurl.h"
#include "formats/ini.h"
#include "lang/eval.h"
#include "lang/interpreter.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "sha_256.h"
#include "wrap.h"

static const char *wrap_field_names[wrap_fields_count] = {
	[wf_directory] = "directory",
	[wf_patch_url] = "patch_url",
	[wf_patch_fallback_url] = "patch_fallback_url",
	[wf_patch_filename] = "patch_filename",
	[wf_patch_hash] = "patch_hash",
	[wf_patch_directory] = "patch_directory",
	[wf_source_url] = "source_url",
	[wf_source_fallback_url] = "source_fallback_url",
	[wf_source_filename] = "source_filename",
	[wf_source_hash] = "source_hash",
	[wf_lead_directory_missing] = "lead_directory_missing",
	[wf_url] = "url",
	[wf_revision] = "revision",
	[wf_depth] = "depth",
	[wf_push_url] = "push_url",
	[wf_clone_recursive] = "clone_recursive",
};

static const char *wrap_type_section_header[wrap_type_count] = {
	[wrap_type_file] = "wrap-file",
	[wrap_type_git] = "wrap-git",
	[wrap_provide] = "provide",
};

struct wrap_parse_ctx {
	struct wrap wrap;
	uint32_t field_lines[wrap_fields_count];
	enum wrap_type section;
	bool have_type;
};

static bool
lookup_wrap_str(const char *s, const char *strs[], uint32_t len, uint32_t *res)
{
	uint32_t i;
	for (i = 0; i < len; ++i) {
		if (strcmp(s, strs[i]) == 0) {
			*res = i;
			return true;
		}
	}

	return false;
}

static bool
wrap_parse_cb(void *_ctx, struct source *src, const char *sect,
	const char *k, const char *v, uint32_t line)
{
	uint32_t res;
	struct wrap_parse_ctx *ctx = _ctx;

	if (!sect) {
		error_messagef(src, line, 1, "key not under wrap section");
		return false;
	} else if (!k) {
		if (!lookup_wrap_str(sect, wrap_type_section_header, wrap_type_count, &res)) {
			error_messagef(src, line, 1, "invalid section '%s'", sect);
			return false;
		}

		ctx->section = res;

		if (res == wrap_provide) {
			return true;
		}

		if (ctx->have_type) {
			error_messagef(src, line, 1, "conflicting wrap types");
			return false;
		}

		ctx->wrap.type = res;
		ctx->have_type = true;
		return true;
	} else if (ctx->section == wrap_provide) {
		L("TODO: handle provide %s = '%s'", k, v);
		return true;
	}

	assert(k && v);

	if (!lookup_wrap_str(k, wrap_field_names, wrap_fields_count, &res)) {
		error_messagef(src, line, 1, "invalid key \"%s\"", k);
		return false;
	} else if (ctx->wrap.fields[res]) {
		error_messagef(src, line, 1, "duplicate key \"%s\"", k);
		return false;
	}

	ctx->wrap.fields[res] = v;
	ctx->field_lines[res] = line;
	return true;
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
fetch_checksum_extract(const char *src, const char *dest, const char *sha256,
	const char *dest_dir)
{
	bool res = false;
	uint8_t *dlbuf = NULL;
	uint64_t dlbuf_len;

	muon_curl_init();

	if (!muon_curl_fetch(src, &dlbuf, &dlbuf_len)) {
		goto ret;
	} else if (!checksum(dlbuf, dlbuf_len, sha256)) {
		goto ret;
	} else if (!muon_archive_extract((const char *)dlbuf, dlbuf_len, dest_dir)) {
		goto ret;
	}

	res = true;
ret:
	if (dlbuf) {
		z_free(dlbuf);
	}
	muon_curl_deinit();
	return res;
}

static bool
validate_wrap(struct wrap_parse_ctx *ctx, const char *file)
{
	uint32_t i;

	enum req { invalid, required, optional };
	enum req field_req[wrap_fields_count] = {
		[wf_directory] = optional,
		[wf_lead_directory_missing] = optional,
		[wf_patch_directory] = optional
	};

	if (ctx->wrap.fields[wf_patch_url]
	    || ctx->wrap.fields[wf_patch_filename]
	    || ctx->wrap.fields[wf_patch_hash]) {
		field_req[wf_patch_url] = required;
		field_req[wf_patch_filename] = required;
		field_req[wf_patch_hash] = required;
		field_req[wf_patch_fallback_url] = optional;
		field_req[wf_patch_directory] = invalid;
	}

	switch (ctx->wrap.type) {
	case wrap_type_file:
		field_req[wf_source_filename] = required;
		field_req[wf_source_url] = required;
		field_req[wf_source_hash] = required;
		field_req[wf_source_fallback_url] = optional;
		break;
	case wrap_type_git:
		field_req[wf_url] = required;
		field_req[wf_revision] = required;
		field_req[wf_depth] = optional;
		field_req[wf_clone_recursive] = optional;
		break;
	default:
		assert(false && "unreachable");
		return false;
	}

	bool valid = true;

	for (i = 0; i < wrap_fields_count; ++i) {
		switch (field_req[i]) {
		case optional:
			break;
		case required:
			if (!ctx->wrap.fields[i]) {
				error_messagef(&ctx->wrap.src, 1, 1, "missing field '%s'", wrap_field_names[i]);
				valid = false;
			}
			break;
		case invalid:
			if (ctx->wrap.fields[i]) {
				error_messagef(&ctx->wrap.src, ctx->field_lines[i], 1, "invalid field");
				valid = false;
			}
			break;
		}
	}

	return valid;
}

void
wrap_destroy(struct wrap *wrap)
{
	fs_source_destroy(&wrap->src);
	if (wrap->buf) {
		z_free(wrap->buf);
		wrap->buf = NULL;
	}
}

bool
wrap_parse(const char *wrap_file, struct wrap *wrap)
{
	struct wrap_parse_ctx ctx = { 0 };

	uint32_t len = strlen(wrap_file);
	assert(len > 5 && "wrap file doesn't end in .wrap??");
	assert(len - 5 < PATH_MAX);
	memcpy(ctx.wrap.name, wrap_file, strlen(wrap_file) - 5);

	if (!ini_parse(wrap_file, &ctx.wrap.src, &ctx.wrap.buf, wrap_parse_cb, &ctx)) {
		wrap_destroy(&ctx.wrap);
		return false;
	}

	if (!validate_wrap(&ctx, wrap_file)) {
		wrap_destroy(&ctx.wrap);
		return false;
	}

	*wrap = ctx.wrap;
	return true;
}

static bool
wrap_apply_patch(struct wrap *wrap, const char *subprojects)
{
	char dest_dir[PATH_MAX];

	const char *dir;
	if (wrap->fields[wf_directory]) {
		dir = wrap->fields[wf_directory];
	} else {
		dir = wrap->name;
	}

	if (!path_join(dest_dir, PATH_MAX, subprojects, dir)) {
		return false;
	}

	if (wrap->fields[wf_patch_url] && !fetch_checksum_extract(wrap->fields[wf_patch_url],
		wrap->fields[wf_patch_filename], wrap->fields[wf_patch_hash], subprojects)) {
		return false;
	} else if (wrap->fields[wf_patch_directory]) {
		char patch_dir[PATH_MAX], buf[PATH_MAX];

		if (!path_join(buf, PATH_MAX, subprojects, "packagefiles")) {
			return false;
		} else if (!path_join(patch_dir, PATH_MAX, buf, wrap->fields[wf_patch_directory])) {
			return false;
		}

		if (!fs_copy_dir(patch_dir, dest_dir)) {
			return false;
		}
	}

	return true;
}

static bool
wrap_handle_git(struct wrap *wrap, const char *subprojects)
{
	LOG_E("TODO: wrap-git");
	return false;
}

static bool
wrap_handle_file(struct wrap *wrap, const char *subprojects)
{
	return fetch_checksum_extract(wrap->fields[wf_source_url],
		wrap->fields[wf_source_filename], wrap->fields[wf_source_hash], subprojects);
}

bool
wrap_handle(const char *wrap_file, const char *subprojects, struct wrap *wrap)
{
	if (!wrap_parse(wrap_file, wrap)) {
		return false;
	}

	switch (wrap->type) {
	case wrap_type_file:
		if (!wrap_handle_file(wrap, subprojects)) {
			return false;
		}
		break;
	case wrap_type_git:
		if (!wrap_handle_git(wrap, subprojects)) {
			return false;
		}
		break;
	default:
		assert(false && "unreachable");
		return false;
	}

	if (!wrap_apply_patch(wrap, subprojects)) {
		return false;
	}

	return true;
}
