/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "external/libarchive.h"
#include "external/libcurl.h"
#include "formats/ini.h"
#include "inttypes.h"
#include "lang/eval.h"
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
	[wf_diff_files] = "diff_files",
	[wf_source_url] = "source_url",
	[wf_source_fallback_url] = "source_fallback_url",
	[wf_source_filename] = "source_filename",
	[wf_source_hash] = "source_hash",
	[wf_lead_directory_missing] = "lead_directory_missing",
	[wf_url] = "url",
	[wf_revision] = "revision",
	[wf_depth] = "depth",
	[wf_push_url] = "push-url",
	[wf_clone_recursive] = "clone-recursive",
	[wf_wrapdb_version] = "wrapdb_version",
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
	struct source_location wrap_field_source_location[wrap_fields_count];
	enum wrap_type section;
	bool have_type;
};

static bool
wrap_parse_cb(void *_ctx,
	struct source *src,
	const char *sect,
	const char *k,
	const char *v,
	struct source_location location)
{
	uint32_t res;
	struct wrap_parse_ctx *ctx = _ctx;

	if (!sect) {
		error_messagef(src, location, log_error, "key not under wrap section");
		return false;
	} else if (!k) {
		if (!lookup_wrap_str(sect, wrap_type_section_header, wrap_type_count, &res)) {
			error_messagef(src, location, log_error, "invalid section '%s'", sect);
			return false;
		}

		ctx->section = res;

		if (res == wrap_provide) {
			ctx->wrap.has_provides = true;
			return true;
		}

		if (ctx->have_type) {
			error_messagef(src, location, log_error, "conflicting wrap types");
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
		error_messagef(src, location, log_error, "invalid key \"%s\"", k);
		return false;
	} else if (ctx->wrap.fields[res]) {
		error_messagef(src, location, log_error, "duplicate key \"%s\"", k);
		return false;
	}

	ctx->wrap.fields[res] = v;
	ctx->wrap_field_source_location[res] = location;
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
	struct source_location location;
};

static void
wrap_check_provide_duplication(struct workspace *wk, struct wrap_parse_provides_ctx *ctx, obj provides, obj key, obj val)
{
	obj oldval;
	if (obj_dict_index(wk, provides, key, &oldval)) {
		static char buf[1024];
		obj_snprintf(wk,
			buf,
			ARRAY_LEN(buf),
			"previous provide for %o from %o, is being overridden by %o",
			key,
			oldval,
			val);

		error_message(ctx->src, ctx->location, log_warn, buf);
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
wrap_parse_provides_cb(void *_ctx,
	struct source *src,
	const char *sect,
	const char *k,
	const char *v,
	struct source_location location)
{
	struct wrap_parse_provides_ctx *ctx = _ctx;
	ctx->src = src;
	ctx->location = location;

	if (!sect) {
		UNREACHABLE_RETURN;
	}

	if (!k) {
		enum wrap_type res;
		if (!lookup_wrap_str(sect, wrap_type_section_header, wrap_type_count, (uint32_t *)&res)) {
			UNREACHABLE_RETURN;
		}

		ctx->section = res;
		return true;
	}

	if (ctx->section != wrap_provide) {
		return true;
	}

	if (!*k) {
		error_messagef(src, location, log_error, "empty provides key \"%s\"", k);
		return false;
	} else if (!*v) {
		error_messagef(src, location, log_error, "empty provides value \"%s\"", v);
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
			    ctx,
			    wrap_parse_provides_cb_add_provides_iter)) {
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
checksum_extract(const char *buf, size_t len, const char *sha256, const char *dest_dir)
{
	if (sha256 && !checksum((const uint8_t *)buf, len, sha256)) {
		return false;
	} else if (!muon_archive_extract(buf, len, dest_dir)) {
		return false;
	}

	return true;
}

static bool
fetch_checksum_extract(const char *src, const char *dest, const char *sha256, const char *dest_dir)
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
wrap_download_or_check_packagefiles(const char *filename,
	const char *url,
	const char *hash,
	const char *dest_dir,
	struct wrap_opts *opts)
{
	bool res = false;
	SBUF_manual(source_path);

	path_join(NULL, &source_path, opts->subprojects, "packagefiles");
	path_push(NULL, &source_path, filename);

	if (fs_file_exists(source_path.buf)) {
		if (!hash) {
			LOG_W("local file '%s' specified without a hash", source_path.buf);
		}

		if (url) {
			LOG_W("url specified, but local file '%s' is being used", source_path.buf);
		}

		struct source src = { 0 };
		if (!fs_read_entire_file(source_path.buf, &src)) {
			goto ret;
		}

		if (!checksum_extract(src.src, src.len, hash, dest_dir)) {
			fs_source_destroy(&src);
			goto ret;
		}

		fs_source_destroy(&src);

		res = true;
	} else if (fs_dir_exists(source_path.buf)) {
		if (url) {
			LOG_W("url specified, but local directory '%s' is being used", source_path.buf);
		}

		if (!fs_copy_dir(source_path.buf, dest_dir)) {
			goto ret;
		}

		res = true;
	} else if (url) {
		if (!opts->allow_download) {
			LOG_E("wrap downloading is disabled");
			goto ret;
		}
		res = fetch_checksum_extract(url, filename, hash, dest_dir);
	} else {
		LOG_E("no url specified, but '%s' is not a file or directory", source_path.buf);
	}

ret:
	sbuf_destroy(&source_path);
	return res;
}

static bool
validate_wrap(struct wrap_parse_ctx *ctx, const char *file)
{
	uint32_t i;

	enum req { invalid, required, optional };
	enum req field_req[wrap_fields_count] = { [wf_directory] = optional,
		[wf_patch_directory] = optional,
		[wf_diff_files] = optional,
		[wf_wrapdb_version] = optional };

	if (ctx->wrap.fields[wf_patch_url] || ctx->wrap.fields[wf_patch_filename] || ctx->wrap.fields[wf_patch_hash]) {
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
		}
		field_req[wf_source_fallback_url] = optional;
		field_req[wf_lead_directory_missing] = optional;
		break;
	case wrap_type_git:
		field_req[wf_url] = required;
		field_req[wf_revision] = required;
		field_req[wf_depth] = optional;
		field_req[wf_clone_recursive] = optional;
		field_req[wf_push_url] = optional;
		break;
	default: assert(false && "unreachable"); return false;
	}

	bool valid = true;

	for (i = 0; i < wrap_fields_count; ++i) {
		switch (field_req[i]) {
		case optional: break;
		case required:
			if (!ctx->wrap.fields[i]) {
				error_messagef(&ctx->wrap.src,
					(struct source_location){ 1, 1 },
					log_error,
					"missing field '%s'",
					wrap_field_names[i]);
				valid = false;
			}
			break;
		case invalid:
			if (ctx->wrap.fields[i]) {
				error_messagef(
					&ctx->wrap.src, ctx->wrap_field_source_location[i], log_error, "invalid field");
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

	sbuf_destroy(&wrap->dest_dir);
	sbuf_destroy(&wrap->name);
}

bool
wrap_parse(const char *wrap_file, struct wrap *wrap)
{
	bool res = false;
	SBUF_manual(subprojects);
	struct wrap_parse_ctx ctx = { 0 };

	if (!ini_parse(wrap_file, &ctx.wrap.src, &ctx.wrap.buf, wrap_parse_cb, &ctx)) {
		goto ret;
	}

	if (!validate_wrap(&ctx, wrap_file)) {
		goto ret;
	}

	*wrap = ctx.wrap;

	sbuf_init(&wrap->dest_dir, wrap->dest_dir_buf, ARRAY_LEN(wrap->dest_dir_buf), sbuf_flag_overflow_alloc);
	sbuf_init(&wrap->name, wrap->name_buf, ARRAY_LEN(wrap->name_buf), sbuf_flag_overflow_alloc);

	path_basename(NULL, &wrap->name, wrap_file);

	const struct str name = { .s = wrap->name.buf, .len = wrap->name.len }, ext = WKSTR(".wrap");

	assert(str_endswith(&name, &ext));

	wrap->name.buf[wrap->name.len - ext.len] = 0;

	const char *dir;
	if (wrap->fields[wf_directory]) {
		dir = wrap->fields[wf_directory];
	} else {
		dir = wrap->name.buf;
	}

	path_dirname(NULL, &subprojects, wrap_file);
	path_join(NULL, &wrap->dest_dir, subprojects.buf, dir);

	res = true;
ret:
	if (!res) {
		wrap_destroy(&ctx.wrap);
	}
	sbuf_destroy(&subprojects);
	return res;
}

static int32_t
wrap_run_cmd_status(char *const *argv, const char *chdir, const char *feed)
{
	struct run_cmd_ctx cmd_ctx = {
		.flags = run_cmd_ctx_flag_dont_capture,
		.chdir = chdir,
		.stdin_path = feed,
	};
	if (!run_cmd_argv(&cmd_ctx, argv, NULL, 0)) {
		return -1;
	}
	run_cmd_ctx_destroy(&cmd_ctx);
	return cmd_ctx.status;
}

static bool
wrap_run_cmd_feed(char *const *argv, const char *chdir, const char *feed)
{
	return wrap_run_cmd_status(argv, chdir, feed) == 0;
}

static bool
wrap_run_cmd(char *const *argv, const char *chdir)
{
	return wrap_run_cmd_status(argv, chdir, 0) == 0;
}

static bool
wrap_apply_diff_files(struct wrap *wrap, const char *dest_dir, struct wrap_opts *opts)
{
	bool res = false;
	SBUF_manual(packagefiles);
	SBUF_manual(diff_path);

	path_join(NULL, &packagefiles, opts->subprojects, "packagefiles");

	char *p = (char *)wrap->fields[wf_diff_files];
	const char *diff_file = p;

	bool patch_cmd_found = fs_has_cmd("patch");

	while (true) {
		if (p[1] == ',' || !p[1]) {
			bool done = !p[1];
			p[1] = 0;

			path_join(NULL, &diff_path, packagefiles.buf, diff_file);

			L("applying diff_file '%s'", diff_path.buf);

			if (patch_cmd_found) {
				if (!wrap_run_cmd_feed(ARGV("patch", "-p1"), wrap->dest_dir.buf, diff_path.buf)) {
					goto ret;
				}
			} else {
				if (!wrap_run_cmd(ARGV("git", "--work-tree", ".", "apply", "-p1", diff_path.buf),
					    wrap->dest_dir.buf)) {
					goto ret;
				}
			}

			if (done) {
				break;
			}

			p += 2;
			while (*p && strchr(" \r\t", *p)) {
				++p;
			}

			if (!*p) {
				break;
			}

			diff_file = p;
		}

		++p;
	}

	res = true;
ret:
	sbuf_destroy(&packagefiles);
	sbuf_destroy(&diff_path);
	return res;
}

static bool
wrap_apply_patch(struct wrap *wrap, struct wrap_opts *opts)
{
	const char *dest_dir, *filename = NULL;
	if (wrap->fields[wf_patch_directory]) {
		dest_dir = wrap->dest_dir.buf;
		filename = wrap->fields[wf_patch_directory];
	} else {
		dest_dir = opts->subprojects;
		filename = wrap->fields[wf_patch_filename];
	}

	if (filename) {
		if (!wrap_download_or_check_packagefiles(
			    filename, wrap->fields[wf_patch_url], wrap->fields[wf_patch_hash], dest_dir, opts)) {
			return false;
		}
	}

	if (wrap->fields[wf_diff_files]) {
		if (!wrap_apply_diff_files(wrap, dest_dir, opts)) {
			return false;
		}
	}

	return true;
}

static bool
wrap_handle_file(struct wrap *wrap, struct wrap_opts *opts)
{
	const char *dest;

	if (!wrap->fields[wf_source_filename]) {
		return false;
	}

	if (wrap->fields[wf_lead_directory_missing]) {
		dest = wrap->dest_dir.buf;
	} else {
		dest = opts->subprojects;
	}

	if (!fs_mkdir(dest, true)) {
		return false;
	}

	return wrap_download_or_check_packagefiles(
		wrap->fields[wf_source_filename], wrap->fields[wf_source_url], wrap->fields[wf_source_hash], dest, opts);
}

static bool
is_git_dir(const char *dir)
{
	SBUF_manual(git_dir);
	path_join(0, &git_dir, dir, ".git");
	bool res = fs_dir_exists(git_dir.buf);
	sbuf_destroy(&git_dir);
	return res;
}

static bool
git_fetch_revision(struct wrap *wrap, const char *depth_str)
{
	return wrap_run_cmd(
		ARGV("git", "fetch", "--depth", depth_str, "origin", wrap->fields[wf_revision]), wrap->dest_dir.buf);
}

static bool
wrap_handle_git(struct wrap *wrap, struct wrap_opts *opts)
{
	int64_t depth = 0;
	char depth_str[64] = { 0 };
	if (wrap->fields[wf_depth]) {
		if (!str_to_i(&WKSTR(wrap->fields[wf_depth]), &depth, true)) {
			LOG_E("invalid value for depth: '%s'", wrap->fields[wf_depth]);
			return false;
		}

		if (strlen(wrap->fields[wf_revision]) != 40) {
			LOG_W("When specifying clone depth you must provide a full git sha as the revision.  Got '%s'",
				wrap->fields[wf_revision]);
			depth = 0;
		}

		// Write it back to a string.  This makes sure we got rid of any whitespace.
		snprintf(depth_str, sizeof(depth_str), "%" PRId64, depth);
	}

	if (is_git_dir(wrap->dest_dir.buf)) {
		if (depth) {
			if (!git_fetch_revision(wrap, depth_str)) {
				return false;
			}
		} else {
			if (!wrap_run_cmd(ARGV("git", "remote", "update"), wrap->dest_dir.buf)) {
				return false;
			}
		}
	} else {
		if (depth) {
			if (!fs_mkdir_p(wrap->dest_dir.buf)) {
				return false;
			} else if (!wrap_run_cmd(ARGV("git", "init"), wrap->dest_dir.buf)) {
				return false;
			} else if (!wrap_run_cmd(ARGV("git", "remote", "add", "origin", wrap->fields[wf_url]),
					   wrap->dest_dir.buf)) {
				return false;
			} else if (!git_fetch_revision(wrap, depth_str)) {
				return false;
			}
		} else {
			if (!wrap_run_cmd(ARGV("git", "clone", wrap->fields[wf_url], wrap->dest_dir.buf), 0)) {
				return false;
			}
		}
	}

	if (!wrap_run_cmd(ARGV("git", "-c", "advice.detachedHead=false", "checkout", wrap->fields[wf_revision], "--"),
		    wrap->dest_dir.buf)) {
		return false;
	}

	return true;
}

bool
git_rev_parse(const char *dir, const char *rev, struct sbuf *out)
{
	struct run_cmd_ctx cmd_ctx = { .chdir = dir };
	const char *const argv[] = { "git", "rev-parse", rev, 0 };

	if (!run_cmd_argv(&cmd_ctx, (char *const *)argv, NULL, 0) || cmd_ctx.status != 0) {
		run_cmd_ctx_destroy(&cmd_ctx);
		LOG_I("git rev-parse %s failed: %s", rev, cmd_ctx.err.buf);
		return false;
	}

	sbuf_clear(out);
	for (uint32_t i = 0; i < cmd_ctx.out.len; ++i) {
		if (is_whitespace(cmd_ctx.out.buf[i])) {
			break;
		}
		sbuf_push(0, out, cmd_ctx.out.buf[i]);
	}

	run_cmd_ctx_destroy(&cmd_ctx);
	return true;
}

static bool
wrap_check_dirty_git(struct wrap *wrap, struct wrap_opts *opts)
{
	if (!is_git_dir(wrap->dest_dir.buf)) {
		wrap->outdated = true;
		return true;
	}

	wrap->dirty = wrap_run_cmd_status(ARGV("git", "diff", "--quiet"), wrap->dest_dir.buf, 0) != 0;

	SBUF_manual(head_rev);
	SBUF_manual(wrap_rev);

	wrap->outdated = true;
	if (git_rev_parse(wrap->dest_dir.buf, "HEAD", &head_rev)
		&& git_rev_parse(wrap->dest_dir.buf, wrap->fields[wf_revision], &wrap_rev)) {
		if (head_rev.len == wrap_rev.len && memcmp(head_rev.buf, wrap_rev.buf, head_rev.len) == 0) {
			wrap->outdated = false;
		}
	}

	sbuf_destroy(&head_rev);
	sbuf_destroy(&wrap_rev);

	return true;
}

static bool
wrap_handle_default(struct wrap *wrap, struct wrap_opts *opts)
{
	bool new_content = false;

	switch (wrap->type) {
	case wrap_type_file:
		if (!fs_dir_exists(wrap->dest_dir.buf) || opts->force_update) {
			new_content = true;

			if (!wrap_handle_file(wrap, opts)) {
				return false;
			}
		}
		break;
	case wrap_type_git:
		if (!is_git_dir(wrap->dest_dir.buf) || opts->force_update) {
			new_content = true;
			if (!opts->allow_download) {
				LOG_E("wrap downloading disabled");
				return false;
			}

			if (!wrap_handle_git(wrap, opts)) {
				return false;
			}
		}
		break;
	default: UNREACHABLE;
	}

	if (new_content || wrap->fields[wf_patch_directory]) {
		if (!wrap_apply_patch(wrap, opts)) {
			return false;
		}
	}
	return true;
}

static bool
wrap_handle_check_dirty(struct wrap *wrap, struct wrap_opts *opts)
{
	if (!fs_dir_exists(wrap->dest_dir.buf)) {
		wrap->outdated = true;
		return true;
	}

	switch (wrap->type) {
	case wrap_type_file:
		// We currently have no way of checking if this wrap type is dirty
		return true;
	case wrap_type_git: {
		return wrap_check_dirty_git(wrap, opts);
	}
	default: UNREACHABLE;
	}
}

static bool
wrap_handle_update(struct wrap *wrap, struct wrap_opts *opts)
{
	if (!wrap_handle_check_dirty(wrap, opts)) {
		return false;
	}

	if (!wrap->outdated) {
		return true;
	}

	if (wrap->dirty) {
		LOG_W("cannot safely update outdated %s because it is dirty", wrap->dest_dir.buf);
	}

	LOG_I("updating %s", wrap->dest_dir.buf);

	opts->force_update = true;
	return wrap_handle_default(wrap, opts);
}

bool
wrap_handle(const char *wrap_file, struct wrap *wrap, struct wrap_opts *opts)
{
	if (!wrap_parse(wrap_file, wrap)) {
		return false;
	}

	switch (opts->mode) {
	case wrap_handle_mode_default: return wrap_handle_default(wrap, opts);
	case wrap_handle_mode_update: return wrap_handle_update(wrap, opts);
	case wrap_handle_mode_check_dirty: return wrap_handle_check_dirty(wrap, opts);
	}

	return true;
}

struct wrap_load_all_ctx {
	struct workspace *wk;
	const char *subprojects;
	struct sbuf *path;
};

static enum iteration_result
wrap_load_all_iter(void *_ctx, const char *file)
{
	struct wrap_load_all_ctx *ctx = _ctx;

	if (!str_endswith(&WKSTR(file), &WKSTR(".wrap"))) {
		return ir_cont;
	}

	path_join(ctx->wk, ctx->path, ctx->subprojects, file);

	if (!fs_file_exists(ctx->path->buf)) {
		return ir_cont;
	}

	struct wrap wrap = { 0 };
	if (!wrap_parse(ctx->path->buf, &wrap)) {
		return ir_err;
	}

	enum iteration_result ret = ir_err;

	if (!wrap.has_provides) {
		ret = ir_cont;
		goto ret;
	}

	struct wrap_parse_provides_ctx wp_ctx = {
		.wk = ctx->wk,
		.wrap = &wrap,
		.wrap_name = make_str(ctx->wk, wrap.name.buf),
	};

	make_obj(ctx->wk, &wp_ctx.wrap_name_arr, obj_array);
	obj_array_push(ctx->wk, wp_ctx.wrap_name_arr, wp_ctx.wrap_name);

	if (!ini_reparse(ctx->path->buf, &wrap.src, wrap.buf, wrap_parse_provides_cb, &wp_ctx)) {
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
	SBUF(wrap_path_buf);

	struct wrap_load_all_ctx ctx = {
		.wk = wk,
		.subprojects = subprojects,
		.path = &wrap_path_buf,
	};

	if (!fs_dir_exists(subprojects)) {
		return true;
	}

	if (!fs_dir_foreach(subprojects, &ctx, wrap_load_all_iter)) {
		return false;
	}

	return true;
}
