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
#include "platform/run_cmd.h"
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

struct wrap_parse_ctx {
	struct wrap wrap;
	uint32_t field_lines[wrap_fields_count];
	enum wrap_type section;
	bool have_type;
};

static bool
wrap_parse_cb(void *_ctx, struct source *src, const char *sect,
	const char *k, const char *v, uint32_t line)
{
	uint32_t res;
	struct wrap_parse_ctx *ctx = _ctx;

	if (!sect) {
		error_messagef(src, line, 1, log_error, "key not under wrap section");
		return false;
	} else if (!k) {
		if (!lookup_wrap_str(sect, wrap_type_section_header, wrap_type_count, &res)) {
			error_messagef(src, line, 1, log_error, "invalid section '%s'", sect);
			return false;
		}

		ctx->section = res;

		if (res == wrap_provide) {
			ctx->wrap.has_provides = true;
			return true;
		}

		if (ctx->have_type) {
			error_messagef(src, line, 1, log_error, "conflicting wrap types");
			return false;
		}

		ctx->wrap.type = res;
		ctx->have_type = true;
		return true;
	} else if (ctx->section == wrap_provide) {
		return true;
	}

	assert(k && v);

	if (!lookup_wrap_str(k, wrap_field_names, wrap_fields_count, &res)) {
		error_messagef(src, line, 1, log_error, "invalid key \"%s\"", k);
		return false;
	} else if (ctx->wrap.fields[res]) {
		error_messagef(src, line, 1, log_error, "duplicate key \"%s\"", k);
		return false;
	}

	ctx->wrap.fields[res] = v;
	ctx->field_lines[res] = line;
	return true;
}

struct wrap_parse_provides_ctx {
	struct workspace *wk;
	const struct wrap *wrap;
	obj wrap_name; // string
	obj wrap_name_arr; // [string]

	enum wrap_type section;
	obj add_provides_tgt;
	struct source *src;
	uint32_t line;
};

static void
wrap_check_provide_duplication(struct workspace *wk,
	struct wrap_parse_provides_ctx *ctx,
	obj provides, obj key, obj val)
{
	obj oldval;
	if (obj_dict_index(wk, provides, key, &oldval)) {
		error_messagef(ctx->src, ctx->line, 1, log_warn,
			"previous provide for %o from %o, is being overriden by %o", key, oldval, val);
	}
}

enum iteration_result
wrap_parse_provides_cb_add_provides_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct wrap_parse_provides_ctx *ctx = _ctx;

	wrap_check_provide_duplication(wk, ctx, ctx->add_provides_tgt, v, ctx->wrap_name_arr);

	obj_dict_set(wk, ctx->add_provides_tgt, v, ctx->wrap_name_arr);
	return ir_cont;
}

static bool
wrap_parse_provides_cb(void *_ctx, struct source *src, const char *sect,
	const char *k, const char *v, uint32_t line)
{
	struct wrap_parse_provides_ctx *ctx = _ctx;
	ctx->src = src;
	ctx->line = line;

	if (!sect) {
		UNREACHABLE_RETURN;
	}

	if (!k) {
		enum wrap_type res;
		if (!lookup_wrap_str(sect, wrap_type_section_header, wrap_type_count, &res)) {
			UNREACHABLE_RETURN;
		}

		ctx->section = res;
		return true;
	}

	if (ctx->section != wrap_provide) {
		return true;
	}

	if (!*k) {
		error_messagef(src, line, 1, log_error, "empty provides key \"%s\"", k);
		return false;
	} else if (!*v) {
		error_messagef(src, line, 1, log_error, "empty provides value \"%s\"", v);
		return false;
	}

	ctx->add_provides_tgt = 0;
	if (strcmp(k, "dependency_names") == 0) {
		ctx->add_provides_tgt = current_project(ctx->wk)->wrap_provides_deps;
	} else if (strcmp(k, "program_names") == 0) {
		ctx->add_provides_tgt = current_project(ctx->wk)->wrap_provides_exes;
	}

	if (ctx->add_provides_tgt) {
		if (!obj_array_foreach(ctx->wk,
			str_split_strip(ctx->wk, &WKSTR(v), &WKSTR(","), NULL),
			ctx, wrap_parse_provides_cb_add_provides_iter)) {
			return false;
		}
	} else {
		obj arr;
		make_obj(ctx->wk, &arr, obj_array);
		obj_array_push(ctx->wk, arr, ctx->wrap_name);
		obj_array_push(ctx->wk, arr, make_str(ctx->wk, v));

		ctx->add_provides_tgt = current_project(ctx->wk)->wrap_provides_deps;
		obj key = make_str(ctx->wk, k);

		wrap_check_provide_duplication(ctx->wk, ctx, ctx->add_provides_tgt, key, arr);
		obj_dict_set(ctx->wk, ctx->add_provides_tgt, key, arr);
	}

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
checksum_extract(const char *buf, size_t len, const char *sha256,
	const char *dest_dir)
{
	if (sha256 && !checksum((const uint8_t *)buf, len, sha256)) {
		return false;
	} else if (!muon_archive_extract(buf, len, dest_dir)) {
		return false;
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
	} else if (!checksum_extract((const char *)dlbuf, dlbuf_len, sha256, dest_dir)) {
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
wrap_download_or_check_packagefiles(const char *filename, const char *url,
	const char *hash, const char *subprojects, const char *dest_dir,
	bool download)
{
	char buf[PATH_MAX], source_path[PATH_MAX];
	if (!path_join(buf, PATH_MAX, subprojects, "packagefiles")) {
		return false;
	}
	if (!path_join(source_path, PATH_MAX, buf, filename)) {
		return false;
	}

	if (fs_file_exists(source_path)) {
		if (!hash) {
			LOG_W("local file '%s' specified without a hash", source_path);
		}

		if (url) {
			LOG_W("url specified, but local file '%s' is being used", source_path);
		}

		struct source src = { 0 };
		if (!fs_read_entire_file(source_path, &src)) {
			return false;
		}

		if (!checksum_extract(src.src, src.len, hash, dest_dir)) {
			fs_source_destroy(&src);
			return false;
		}

		fs_source_destroy(&src);

		return true;
	} else if (fs_dir_exists(source_path)) {
		if (url) {
			LOG_W("url specified, but local directory '%s' is being used", source_path);
		}

		if (!fs_copy_dir(source_path, dest_dir)) {
			return false;
		}

		return true;
	} else if (url) {
		if (!download) {
			LOG_E("wrap downloading is disabled");
			return false;
		}
		return fetch_checksum_extract(url, filename, hash, dest_dir);
	} else {
		LOG_E("no url specified, but '%s' is not a file or directory", source_path);
		return false;
	}
}

static bool
validate_wrap(struct wrap_parse_ctx *ctx, const char *file)
{
	uint32_t i;

	enum req { invalid, required, optional };
	enum req field_req[wrap_fields_count] = {
		[wf_directory] = optional,
		[wf_patch_directory] = optional
	};

	if (ctx->wrap.fields[wf_patch_url]
	    || ctx->wrap.fields[wf_patch_filename]
	    || ctx->wrap.fields[wf_patch_hash]) {
		field_req[wf_patch_url] = optional;
		field_req[wf_patch_filename] = required;
		field_req[wf_patch_hash] = optional;
		field_req[wf_patch_fallback_url] = optional;
		field_req[wf_patch_directory] = invalid;
	}

	switch (ctx->wrap.type) {
	case wrap_type_file:
		field_req[wf_source_filename] = optional;
		field_req[wf_source_url] = optional;
		field_req[wf_source_hash] = optional;
		if (ctx->wrap.fields[wf_source_url]) {
			field_req[wf_source_filename] = required;
			field_req[wf_source_hash] = required;
		}
		field_req[wf_source_fallback_url] = optional;
		field_req[wf_lead_directory_missing] = optional;
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
				error_messagef(&ctx->wrap.src, 1, 1, log_error, "missing field '%s'", wrap_field_names[i]);
				valid = false;
			}
			break;
		case invalid:
			if (ctx->wrap.fields[i]) {
				error_messagef(&ctx->wrap.src, ctx->field_lines[i], 1, log_error, "invalid field");
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

	if (!ini_parse(wrap_file, &ctx.wrap.src, &ctx.wrap.buf, wrap_parse_cb, &ctx)) {
		wrap_destroy(&ctx.wrap);
		return false;
	}

	if (!validate_wrap(&ctx, wrap_file)) {
		wrap_destroy(&ctx.wrap);
		return false;
	}

	*wrap = ctx.wrap;

	if (!path_basename(wrap->name, PATH_MAX, wrap_file)) {
		return false;
	}

	const struct str *name = &WKSTR(wrap->name);
	assert(str_endswith(name, &WKSTR(".wrap")));

	wrap->name[name->len - 5] = 0;

	const char *dir;
	if (wrap->fields[wf_directory]) {
		dir = wrap->fields[wf_directory];
	} else {
		dir = wrap->name;
	}

	char subprojects[PATH_MAX];
	if (!path_dirname(subprojects, PATH_MAX, wrap_file)) {
		return false;
	} else if (!path_join(wrap->dest_dir, PATH_MAX, subprojects, dir)) {
		return false;
	}

	return true;
}

static bool
wrap_apply_patch(struct wrap *wrap, const char *subprojects, bool download)
{
	const char *dest_dir, *filename = NULL;
	if (wrap->fields[wf_patch_directory]) {
		dest_dir = wrap->dest_dir;
		filename = wrap->fields[wf_patch_directory];
	} else {
		dest_dir = subprojects;
		filename = wrap->fields[wf_patch_filename];
	}

	if (!filename) {
		return true;
	}

	return wrap_download_or_check_packagefiles(
		filename,
		wrap->fields[wf_patch_url],
		wrap->fields[wf_patch_hash],
		subprojects,
		dest_dir,
		download
		);
}

static bool
run_git(const char *const argv[])
{
	struct run_cmd_ctx cmd_ctx = { .flags = run_cmd_ctx_flag_dont_capture };

	if (!run_cmd_argv(&cmd_ctx, "git", (char *const *)argv, NULL)) {
		return false;
	} else if (cmd_ctx.status != 0) {
		run_cmd_ctx_destroy(&cmd_ctx);
		return false;
	} else {
		run_cmd_ctx_destroy(&cmd_ctx);
		return true;
	}
}

static bool
wrap_handle_git(struct wrap *wrap, const char *subprojects)
{
	if (!run_git((const char *const[]) {
		"git", "clone", wrap->fields[wf_url], wrap->dest_dir,
		NULL
	})) {
		return false;
	}

	if (!run_git((const char *const[]) {
		"git", "-C", wrap->dest_dir, "-c", "advice.detachedHead=false", "checkout", wrap->fields[wf_revision], "--",
		NULL
	})) {
		return false;
	}

	return true;
}

static bool
wrap_handle_file(struct wrap *wrap, const char *subprojects, bool download)
{
	const char *dest;

	if (wrap->fields[wf_lead_directory_missing]) {
		dest = wrap->dest_dir;
	} else {
		dest = subprojects;
	}

	if (!fs_dir_exists(dest)) {
		if (!fs_mkdir(dest)) {
			return false;
		}
	}

	return wrap_download_or_check_packagefiles(
		wrap->fields[wf_source_filename],
		wrap->fields[wf_source_url],
		wrap->fields[wf_source_hash],
		subprojects,
		dest,
		download
		);
}

bool
wrap_handle(const char *wrap_file, const char *subprojects, struct wrap *wrap, bool download)
{
	if (!wrap_parse(wrap_file, wrap)) {
		return false;
	}

	char meson_build[PATH_MAX];
	if (!path_join(meson_build, PATH_MAX, wrap->dest_dir, "meson.build")) {
		return false;
	} else if (fs_file_exists(meson_build)) {
		char basename[PATH_MAX];
		if (!path_basename(basename, PATH_MAX, wrap_file)) {
			return false;
		}

		return true;
	}

	switch (wrap->type) {
	case wrap_type_file:
		if (!wrap_handle_file(wrap, subprojects, download)) {
			return false;
		}
		break;
	case wrap_type_git:
		if (!download) {
			LOG_E("wrap downloading disabled");
			return false;
		}

		if (!wrap_handle_git(wrap, subprojects)) {
			return false;
		}
		break;
	default:
		assert(false && "unreachable");
		return false;
	}

	if (!wrap_apply_patch(wrap, subprojects, download)) {
		return false;
	}

	return true;
}

struct wrap_load_all_ctx {
	struct workspace *wk;
	const char *subprojects;
};

static enum iteration_result
wrap_load_all_iter(void *_ctx, const char *file)
{
	struct wrap_load_all_ctx *ctx = _ctx;
	char path[PATH_MAX];

	if (!str_endswith(&WKSTR(file), &WKSTR(".wrap"))) {
		return ir_cont;
	} else if (!path_join(path, PATH_MAX, ctx->subprojects, file)) {
		return ir_err;
	} else if (!fs_file_exists(path)) {
		return ir_cont;
	}

	struct wrap wrap = { 0 };
	if (!wrap_parse(path, &wrap)) {
		return ir_err;
	}

	if (!wrap.has_provides) {
		return ir_cont;
	}

	struct wrap_parse_provides_ctx wp_ctx = {
		.wk = ctx->wk,
		.wrap = &wrap,
		.wrap_name = make_str(ctx->wk, wrap.name),
	};

	make_obj(ctx->wk, &wp_ctx.wrap_name_arr, obj_array);
	obj_array_push(ctx->wk, wp_ctx.wrap_name_arr, wp_ctx.wrap_name);

	enum iteration_result ret = ir_err;
	if (!ini_reparse(path, &wrap.src, wrap.buf, wrap_parse_provides_cb, &wp_ctx)) {
		goto ret;
	}

	ret = ir_cont;
ret:
	wrap_destroy(&wrap);
	return ret;
}

bool
wrap_load_all_provides(struct workspace *wk, const char *subprojects)
{
	struct wrap_load_all_ctx ctx = {
		.wk = wk,
		.subprojects = subprojects,
	};

	if (!fs_dir_exists(subprojects)) {
		return true;
	}

	if (!fs_dir_foreach(subprojects, &ctx, wrap_load_all_iter)) {
		return false;
	}

	return true;
}
