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
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "sha_256.h"
#include "tracy.h"
#include "wrap.h"

static const char *meson_subproject_wrap_hash_txt = ".meson-subproject-wrap-hash.txt";

static const char *wrap_field_names[wrap_fields_count] = {
	[wf_directory] = "directory",
	[wf_patch_url] = "patch_url",
	[wf_patch_fallback_url] = "patch_fallback_url",
	[wf_patch_filename] = "patch_filename",
	[wf_patch_hash] = "patch_hash",
	[wf_patch_directory] = "patch_directory",
	[wf_diff_files] = "diff_files",
	[wf_method] = "method",
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

enum wrap_handle_state {
	wrap_handle_state_init,

	wrap_handle_state_check_dirty,
	wrap_handle_state_check_dirty_git_parse_head,
	wrap_handle_state_check_dirty_git_parse_rev,
	wrap_handle_state_check_dirty_git_cmp_rev,

	wrap_handle_state_update,

	wrap_handle_state_file_init,

	wrap_handle_state_git_init,
	wrap_handle_state_git_fetch,
	wrap_handle_state_git_fetch_fallback,
	wrap_handle_state_git_checkout,
	wrap_handle_state_git_done,

	wrap_handle_state_apply_patch,

	wrap_handle_state_done,
};

const char *
wrap_handle_state_to_s(uint32_t state)
{
	switch ((enum wrap_handle_state)state) {
	case wrap_handle_state_init: return "init";

	case wrap_handle_state_check_dirty: return "check_dirty";
	case wrap_handle_state_check_dirty_git_parse_head: return "check_dirty git parse head";
	case wrap_handle_state_check_dirty_git_parse_rev: return "check_dirty git parse rev";
	case wrap_handle_state_check_dirty_git_cmp_rev: return "check_dirty git cmp rev";

	case wrap_handle_state_update: return "update";

	case wrap_handle_state_file_init: return "file";

	case wrap_handle_state_git_init: return "git init";
	case wrap_handle_state_git_fetch: return "git fetch";
	case wrap_handle_state_git_fetch_fallback: return "git fetch fallback";
	case wrap_handle_state_git_checkout: return "git checkout";
	case wrap_handle_state_git_done: return "git done";

	case wrap_handle_state_apply_patch: return "apply patch";

	case wrap_handle_state_done: return "done";
	default: UNREACHABLE_RETURN;
	}
}

MUON_ATTR_FORMAT(printf, 3, 4)
static void
wrap_log(struct wrap_handle_ctx *ctx, enum log_level lvl, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	log_print(false, lvl, "%s.wrap: ", ctx->wrap.name.buf);
	log_printv(lvl, fmt, args);
	log_plain(lvl, "\n");

	va_end(args);
}

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

		error_message(ctx->src, ctx->location, log_warn, 0, buf);
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
		error_messagef(src, location, log_warn, "empty provides value \"%s\"", v);
		return true;
	}

	ctx->add_provides_tgt = 0;
	if (strcmp(k, "dependency_names") == 0) {
		ctx->add_provides_tgt = current_project(ctx->wk)->wrap_provides_deps;
	} else if (strcmp(k, "program_names") == 0) {
		ctx->add_provides_tgt = current_project(ctx->wk)->wrap_provides_exes;
	}

	if (ctx->add_provides_tgt) {
		if (!obj_array_foreach(ctx->wk,
			    str_split_strip(ctx->wk, &STRL(v), &STR(","), NULL),
			    ctx,
			    wrap_parse_provides_cb_add_provides_iter)) {
			return false;
		}
	} else {
		obj arr;
		arr = make_obj(ctx->wk, obj_array);
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
wrap_checksum(struct workspace *wk,
	struct wrap_handle_ctx *ctx,
	const uint8_t *file_buf,
	uint64_t len,
	const char *sha256)
{
	char buf[3] = { 0 };
	uint32_t i;
	uint8_t b, hash[32];

	if (strlen(sha256) != 64) {
		wrap_log(ctx, log_error, "checksum '%s' is not 64 characters long", sha256);
		return false;
	}

	calc_sha_256(hash, file_buf, len);

	for (i = 0; i < 64; i += 2) {
		memcpy(buf, &sha256[i], 2);
		b = strtol(buf, NULL, 16);
		if (b != hash[i / 2]) {
			char buf[65];
			sha256_to_str(hash, buf);
			wrap_log(ctx, log_error, "checksum mismatch: '%s' != '%s'", sha256, buf);
			return false;
		}
	}

	return true;
}

static void
wrap_copy_packagefiles_file_cb(void *_ctx, const char *src, const char *dest)
{
	struct workspace *wk = _ctx;
	workspace_add_regenerate_dep(wk, make_str(wk, src));
	workspace_add_exclude_regenerate_dep(wk, make_str(wk, dest));
}

static bool
wrap_copy_packagefiles_dir(struct workspace *wk, const char *src_base, const char *dest_base)
{
	struct fs_copy_dir_ctx ctx = {
		.file_cb = wrap_copy_packagefiles_file_cb,
		.usr_ctx = wk,
		.src_base = src_base,
		.dest_base = dest_base,
		.force = true,
	};

	return fs_copy_dir_ctx(&ctx);
}

enum wrap_checksum_extract_local_file_result {
	wrap_checksum_extract_local_file_result_failed,
	wrap_checksum_extract_local_file_result_checksum_mismatch,
	wrap_checksum_extract_local_file_result_ok,
};

static enum wrap_checksum_extract_local_file_result
wrap_checksum_extract_local_file(struct workspace *wk,
	struct wrap_handle_ctx *ctx,
	const char *source_path,
	const char *hash,
	const char *dest_dir,
	bool ignore_if_hash_mismatch)
{
	struct source src = { 0 };
	if (!fs_read_entire_file(source_path, &src)) {
		return wrap_checksum_extract_local_file_result_failed;
	}

	if (hash) {
		if (!wrap_checksum(wk, ctx, (const uint8_t *)src.src, src.len, hash)) {
			fs_source_destroy(&src);

			if (ignore_if_hash_mismatch) {
				return wrap_checksum_extract_local_file_result_checksum_mismatch;
			}
			return wrap_checksum_extract_local_file_result_failed;
		}
	}

	bool ok = muon_archive_extract(src.src, src.len, dest_dir);
	fs_source_destroy(&src);
	return ok ? wrap_checksum_extract_local_file_result_ok : wrap_checksum_extract_local_file_result_failed;
}

static bool
wrap_download_or_check_packagefiles(struct workspace *wk,
	const char *filename,
	const char *url,
	const char *hash,
	const char *dest_dir,
	struct wrap_handle_ctx *ctx)
{
	TSTR(source_path);
	path_join(wk, &source_path, ctx->opts.subprojects, "packagefiles");
	path_push(wk, &source_path, filename);

	if (fs_file_exists(source_path.buf)) {
		if (!hash) {
			wrap_log(ctx, log_warn, "local file '%s' specified without a hash", source_path.buf);
		}

		if (url) {
			wrap_log(ctx, log_warn, "url specified, but local file '%s' is being used", source_path.buf);
		}

		if (!wrap_checksum_extract_local_file(wk, ctx, source_path.buf, hash, dest_dir, false)) {
			return false;
		}
	} else if (fs_dir_exists(source_path.buf)) {
		if (url) {
			wrap_log(ctx,
				log_warn,
				"url specified, but local directory '%s' is being used",
				source_path.buf);
		}

		if (!wrap_copy_packagefiles_dir(wk, source_path.buf, dest_dir)) {
			return false;
		}
	} else if (url) {
		// If a hash is specified and has already been downloaded, just use that instead.
		if (hash) {
			path_join(wk, &source_path, ctx->opts.subprojects, "packagecache");
			path_push(wk, &source_path, filename);

			if (fs_file_exists(source_path.buf)) {
				switch (wrap_checksum_extract_local_file(
					wk, ctx, source_path.buf, hash, dest_dir, true)) {
				case wrap_checksum_extract_local_file_result_failed: return false;
				case wrap_checksum_extract_local_file_result_ok: return true;
				case wrap_checksum_extract_local_file_result_checksum_mismatch:
					L("ignoring cached file %s: checksum does not match", source_path.buf);
					break;
				}
			}
		}

		if (!ctx->opts.allow_download) {
			wrap_log(ctx, log_error, "wrap downloading is disabled");
			return false;
		}

		enum mc_fetch_flag flags = 0;
		if (ctx->opts.block) {
			flags |= mc_fetch_flag_verbose;
		}

		ctx->fetch_ctx.handle = mc_fetch_begin(url, &ctx->fetch_ctx.buf, &ctx->fetch_ctx.len, flags);
		ctx->fetch_ctx.hash = hash;
		ctx->fetch_ctx.dest_dir = dest_dir;
		ctx->fetch_ctx.filename = filename;

		if (ctx->fetch_ctx.handle == -1) {
			return false;
		}

		ctx->sub_state = wrap_handle_sub_state_fetching;
	} else {
		wrap_log(ctx, log_error, "no url specified, but '%s' is not a file or directory", source_path.buf);
	}

	return true;
}

static bool
validate_wrap(struct wrap_parse_ctx *ctx, const char *file)
{
	uint32_t i;

	enum req { invalid, required, optional };
	enum req field_req[wrap_fields_count] = {
		[wf_directory] = optional,
		[wf_patch_directory] = optional,
		[wf_diff_files] = optional,
		[wf_wrapdb_version] = optional,
		[wf_method] = optional,
	};

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
}

bool
wrap_parse(struct workspace *wk, const char *subprojects, const char *wrap_file, struct wrap *wrap)
{
	bool res = false;
	struct wrap_parse_ctx ctx = { 0 };

	if (!ini_parse(wrap_file, &ctx.wrap.src, &ctx.wrap.buf, wrap_parse_cb, &ctx)) {
		goto ret;
	}

	if (!validate_wrap(&ctx, wrap_file)) {
		goto ret;
	}

	*wrap = ctx.wrap;

	tstr_init(&wrap->dest_dir, wrap->dest_dir_buf, ARRAY_LEN(wrap->dest_dir_buf), 0);
	tstr_init(&wrap->name, wrap->name_buf, ARRAY_LEN(wrap->name_buf), 0);

	path_basename(wk, &wrap->name, wrap_file);

	const struct str name = { .s = wrap->name.buf, .len = wrap->name.len }, ext = STR(".wrap");

	assert(str_endswith(&name, &ext));

	wrap->name.buf[wrap->name.len - ext.len] = 0;

	const char *dir;
	if (wrap->fields[wf_directory]) {
		dir = wrap->fields[wf_directory];
	} else {
		dir = wrap->name.buf;
	}

	path_join(wk, &wrap->dest_dir, subprojects, dir);

	res = true;
ret:
	if (!res) {
		wrap_destroy(&ctx.wrap);
	}
	return res;
}

enum wrap_run_cmd_flag {
	wrap_run_cmd_flag_yield = 1 << 0,
	wrap_run_cmd_flag_allow_failure = 1 << 1,
};

static bool
wrap_run_cmd_finish(struct workspace *wk, struct wrap_handle_ctx *ctx, bool error)
{
	if (ctx->cmd_ctx.status != 0 && !ctx->run_cmd_opts.allow_failure) {
		error = true;
	}

	if (error) {
		wrap_log(ctx, log_error, "command '%s' failed", ctx->run_cmd_opts.cmdstr);
		if (ctx->cmd_ctx.out.len) {
			log_plain(log_error, "stdout:\n%s", ctx->cmd_ctx.out.buf);
		}
		if (ctx->cmd_ctx.err.len) {
			log_plain(log_error, "stderr:\n%s", ctx->cmd_ctx.err.buf);
		}
	} else if (ctx->run_cmd_opts.out) {
		tstr_clear(ctx->run_cmd_opts.out);
		for (uint32_t i = 0; i < ctx->cmd_ctx.out.len; ++i) {
			if (is_whitespace(ctx->cmd_ctx.out.buf[i])) {
				break;
			}
			tstr_push(wk, ctx->run_cmd_opts.out, ctx->cmd_ctx.out.buf[i]);
		}
		ctx->run_cmd_opts.out = 0;
	}

	run_cmd_ctx_destroy(&ctx->cmd_ctx);

	if (error) {
		return false;
	}

	return true;
}

static int32_t
wrap_run_cmd_status(struct workspace *wk,
	struct wrap_handle_ctx *ctx,
	char *const *argv,
	const char *chdir,
	const char *feed,
	enum wrap_run_cmd_flag flags)
{
	if (ctx->opts.block) {
		flags &= ~wrap_run_cmd_flag_yield;
	}

	ctx->run_cmd_opts.allow_failure = flags & wrap_run_cmd_flag_allow_failure;

	ctx->cmd_ctx = (struct run_cmd_ctx){
		.flags = run_cmd_ctx_flag_dont_capture,
		.chdir = chdir,
		.stdin_path = feed,
	};

	if (flags & wrap_run_cmd_flag_yield) {
		ctx->cmd_ctx.flags = run_cmd_ctx_flag_async;
	}

	{
		uint32_t i, bufi = 0;
		for (i = 0; argv[i]; ++i) {
			snprintf_append(ctx->run_cmd_opts.cmdstr, &bufi, "%s%s", argv[i], argv[i + 1] ? " " : "");
		}
	}

	if (!run_cmd_argv(&ctx->cmd_ctx, argv, 0, 0)) {
		if (!(ctx->run_cmd_opts.allow_failure)) {
			wrap_log(ctx,
				log_error,
				"command '%s' failed: %s",
				ctx->run_cmd_opts.cmdstr,
				ctx->cmd_ctx.err_msg);
		}
		return -1;
	}

	if (flags & wrap_run_cmd_flag_yield) {
		ctx->sub_state = wrap_handle_sub_state_running_cmd;
		return 0;
	}

	if (!wrap_run_cmd_finish(wk, ctx, false)) {
		return -1;
	}

	if (flags & wrap_run_cmd_flag_allow_failure) {
		return 0;
	}

	return ctx->cmd_ctx.status;
}

static bool
wrap_run_cmd_feed(struct workspace *wk,
	struct wrap_handle_ctx *ctx,
	char *const *argv,
	const char *chdir,
	const char *feed,
	enum wrap_run_cmd_flag flags)
{
	return wrap_run_cmd_status(wk, ctx, argv, chdir, feed, flags) == 0;
}

static bool
wrap_run_cmd(struct workspace *wk,
	struct wrap_handle_ctx *ctx,
	char *const *argv,
	const char *chdir,
	enum wrap_run_cmd_flag flags)
{
	return wrap_run_cmd_status(wk, ctx, argv, chdir, 0, flags) == 0;
}

static bool
wrap_apply_diff_files(struct workspace *wk, struct wrap_handle_ctx *ctx)
{
	TSTR(packagefiles);
	TSTR(diff_path);

	path_join(wk, &packagefiles, ctx->opts.subprojects, "packagefiles");

	char *p = (char *)ctx->wrap.fields[wf_diff_files];
	const char *diff_file = p;

	bool patch_cmd_found = fs_has_cmd("patch");

	while (true) {
		if (p[1] == ',' || !p[1]) {
			bool done = !p[1];
			p[1] = 0;

			path_join(wk, &diff_path, packagefiles.buf, diff_file);

			wrap_log(ctx, log_debug, "applying diff_file '%s'", diff_path.buf);

			if (patch_cmd_found) {
				if (!wrap_run_cmd_feed(
					    wk, ctx, ARGV("patch", "-p1"), ctx->wrap.dest_dir.buf, diff_path.buf, 0)) {
					return false;
				}
			} else {
				if (!wrap_run_cmd(wk,
					    ctx,
					    ARGV("git", "--work-tree", ".", "apply", "-p1", diff_path.buf),
					    ctx->wrap.dest_dir.buf,
					    0)) {
					return false;
				}
			}

			if (done) {
				break;
			}

			p += 2;
			while (*p && is_whitespace_except_newline(*p)) {
				++p;
			}

			if (!*p) {
				break;
			}

			diff_file = p;
		}

		++p;
	}

	return true;
}

static bool
wrap_apply_patch(struct workspace *wk, struct wrap_handle_ctx *ctx)
{
	if ((ctx->wrap.fields[wf_diff_files] || ctx->wrap.fields[wf_patch_url]) && !ctx->wrap.updated) {
		// Only apply patches with diff files or from urls after the wrap has
		// been updated, not every time.
		return true;
	}

	wrap_log(ctx, log_debug, "applying patch");

	const char *dest_dir, *filename = NULL;
	if (ctx->wrap.fields[wf_patch_directory]) {
		dest_dir = ctx->wrap.dest_dir.buf;
		filename = ctx->wrap.fields[wf_patch_directory];
	} else {
		dest_dir = ctx->opts.subprojects;
		filename = ctx->wrap.fields[wf_patch_filename];
	}

	if (filename) {
		if (!wrap_download_or_check_packagefiles(wk,
			    filename,
			    ctx->wrap.fields[wf_patch_url],
			    ctx->wrap.fields[wf_patch_hash],
			    dest_dir,
			    ctx)) {
			return false;
		}
	}

	if (ctx->wrap.fields[wf_diff_files]) {
		if (!wrap_apply_diff_files(wk, ctx)) {
			return false;
		}
	}

	return true;
}

static void
wrap_set_state(struct wrap_handle_ctx *ctx, enum wrap_handle_state state)
{
	ctx->prev_state = ctx->state;
	ctx->state = state;
}

static bool
wrap_handle_file(struct workspace *wk, struct wrap_handle_ctx *ctx)
{
	wrap_set_state(ctx, wrap_handle_state_apply_patch);
	ctx->wrap.updated = true;

	const char *dest;
	if (!ctx->wrap.fields[wf_source_filename]) {
		return false;
	}

	if (ctx->wrap.fields[wf_lead_directory_missing]) {
		dest = ctx->wrap.dest_dir.buf;
	} else {
		dest = ctx->opts.subprojects;
	}

	if (!fs_mkdir(dest, true)) {
		return false;
	}

	return wrap_download_or_check_packagefiles(wk,
		ctx->wrap.fields[wf_source_filename],
		ctx->wrap.fields[wf_source_url],
		ctx->wrap.fields[wf_source_hash],
		dest,
		ctx);
}

static bool
wrap_hash(const char *wrap_file, char buf[65])
{
	struct source src;
	if (!fs_read_entire_file(wrap_file, &src)) {
		return false;
	}

	uint8_t hash[32];
	calc_sha_256(hash, src.src, src.len);
	sha256_to_str(hash, buf);
	fs_source_destroy(&src);

	return true;
}

static bool
is_git_dir(struct workspace *wk, const char *dir)
{
	TSTR(git_dir);
	path_join(wk, &git_dir, dir, ".git");
	bool res = fs_dir_exists(git_dir.buf);
	return res;
}

static bool
git_fetch_revision(struct workspace *wk, struct wrap_handle_ctx *ctx, const char *depth_str)
{
	return wrap_run_cmd(wk,
		ctx,
		ARGV("git", "fetch", "--depth", depth_str, "--progress", "origin", ctx->wrap.fields[wf_revision]),
		ctx->wrap.dest_dir.buf,
		wrap_run_cmd_flag_yield | wrap_run_cmd_flag_allow_failure);
}

static bool
wrap_git_init(struct workspace *wk, struct wrap_handle_ctx *ctx)
{
	if (!fs_mkdir_p(ctx->wrap.dest_dir.buf)) {
		return false;
	} else if (!wrap_run_cmd(wk, ctx, ARGV("git", "init", "-q"), ctx->wrap.dest_dir.buf, 0)) {
		return false;
	} else if (!wrap_run_cmd(wk,
			   ctx,
			   ARGV("git", "remote", "add", "origin", ctx->wrap.fields[wf_url]),
			   ctx->wrap.dest_dir.buf,
			   0)) {
		return false;
	}

	return true;
}

static bool
wrap_handle_git(struct workspace *wk, struct wrap_handle_ctx *ctx)
{
	switch (ctx->state) {
	case wrap_handle_state_git_init: {
		if (ctx->wrap.fields[wf_depth]) {
			if (!str_to_i(&STRL(ctx->wrap.fields[wf_depth]), &ctx->git.depth, true)) {
				wrap_log(ctx, log_error, "invalid value for depth: '%s'", ctx->wrap.fields[wf_depth]);
				return false;
			}

			if (strlen(ctx->wrap.fields[wf_revision]) != 40) {
				wrap_log(ctx,
					log_warn,
					"When specifying clone depth you must provide a full git sha as the revision.  Got '%s'",
					ctx->wrap.fields[wf_revision]);
				ctx->git.depth = 0;
			}

			// Write it back to a string.  This makes sure we got rid of any whitespace.
			snprintf(ctx->git.depth_str, sizeof(ctx->git.depth_str), "%" PRId64, ctx->git.depth);
		}

		wrap_set_state(ctx, wrap_handle_state_git_fetch);
		return true;
	}
	case wrap_handle_state_git_fetch: {
		if (is_git_dir(wk, ctx->wrap.dest_dir.buf)) {
			if (!wrap_run_cmd(wk,
				    ctx,
				    ARGV("git", "remote", "set-url", "origin", ctx->wrap.fields[wf_url]),
				    ctx->wrap.dest_dir.buf,
				    0)) {
				return false;
			}

			if (ctx->git.depth) {
				if (!git_fetch_revision(wk, ctx, ctx->git.depth_str)) {
					return false;
				}
			} else {
				if (!wrap_run_cmd(wk,
					    ctx,
					    ARGV("git", "remote", "update"),
					    ctx->wrap.dest_dir.buf,
					    wrap_run_cmd_flag_yield)) {
					return false;
				}
			}
		} else if (ctx->git.depth) {
			if (!wrap_git_init(wk, ctx)) {
				return false;
			} else if (!git_fetch_revision(wk, ctx, ctx->git.depth_str)) {
				return false;
			}
		} else {
			if (!wrap_run_cmd(wk,
				    ctx,
				    ARGV("git", "clone", "--progress", ctx->wrap.fields[wf_url], ctx->wrap.dest_dir.buf),
				    0,
				    wrap_run_cmd_flag_yield | wrap_run_cmd_flag_allow_failure)) {
				return false;
			}
		}

		wrap_set_state(ctx, wrap_handle_state_git_fetch_fallback);
		return true;
	}
	case wrap_handle_state_git_fetch_fallback: {
		if (ctx->cmd_ctx.status != 0) {
			wrap_log(ctx, log_warn, "Shallow clone failed, falling back to full clone.");
			if (!is_git_dir(wk, ctx->wrap.dest_dir.buf)) {
				if (!wrap_git_init(wk, ctx)) {
					return false;
				}
			}

			if (!wrap_run_cmd(wk,
				    ctx,
				    ARGV("git", "fetch", "origin"),
				    ctx->wrap.dest_dir.buf,
				    wrap_run_cmd_flag_yield)) {
				return false;
			}
		}
		wrap_set_state(ctx, wrap_handle_state_git_checkout);
		return true;
	}
	case wrap_handle_state_git_checkout: {
		if (!wrap_run_cmd(wk,
			    ctx,
			    ARGV("git",
				    "-c",
				    "advice.detachedHead=false",
				    "checkout",
				    ctx->wrap.fields[wf_revision],
				    "--"),
			    ctx->wrap.dest_dir.buf,
			    wrap_run_cmd_flag_yield)) {
			return false;
		}

		wrap_set_state(ctx, wrap_handle_state_git_done);
		return true;
	}
	case wrap_handle_state_git_done: {
		ctx->wrap.updated = true;
		wrap_set_state(ctx, wrap_handle_state_apply_patch);
		return true;
	}
	default: UNREACHABLE;
	}
}

static bool
wrap_git_rev_parse(struct workspace *wk, struct wrap_handle_ctx *ctx, const char *dir, const char *rev, struct tstr *out)
{
	const char *const argv[] = { "git", "rev-parse", rev, 0 };

	ctx->run_cmd_opts.out = out;

	return wrap_run_cmd(
		wk, ctx, (char *const *)argv, dir, wrap_run_cmd_flag_yield | wrap_run_cmd_flag_allow_failure);
}

static bool
wrap_handle_default(struct workspace *wk, struct wrap_handle_ctx *ctx)
{
	switch (ctx->opts.mode) {
	case wrap_handle_mode_default: {
		{
			switch (ctx->wrap.type) {
			case wrap_type_file:
				if (!fs_dir_exists(ctx->wrap.dest_dir.buf) || ctx->opts.force_update) {
					wrap_set_state(ctx, wrap_handle_state_file_init);
				} else {
					wrap_set_state(ctx, wrap_handle_state_apply_patch);
				}
				break;
			case wrap_type_git:
				if (!is_git_dir(wk, ctx->wrap.dest_dir.buf) || ctx->opts.force_update) {
					if (!ctx->opts.allow_download) {
						wrap_log(ctx, log_error, "wrap downloading disabled");
						return false;
					}
					wrap_set_state(ctx, wrap_handle_state_git_init);
				} else {
					wrap_set_state(ctx, wrap_handle_state_apply_patch);
				}
				break;
			default: UNREACHABLE;
			}

			return true;
		}
	}
	case wrap_handle_mode_update: wrap_set_state(ctx, wrap_handle_state_check_dirty); break;
	case wrap_handle_mode_check_dirty: wrap_set_state(ctx, wrap_handle_state_check_dirty); break;
	}

	return true;
}

static bool
wrap_handle_check_dirty_next_state(struct workspace *wk, struct wrap_handle_ctx *ctx)
{
	if (ctx->opts.mode == wrap_handle_mode_check_dirty) {
		wrap_set_state(ctx, wrap_handle_state_done);
	} else {
		wrap_set_state(ctx, wrap_handle_state_update);
	}

	return true;
}

void
wrap_handle_async_start(struct workspace *wk)
{
	mc_init();
}

void
wrap_handle_async_end(struct workspace *wk)
{
	mc_deinit();
}

bool
wrap_handle_async(struct workspace *wk, const char *wrap_file, struct wrap_handle_ctx *ctx)
{
	if (ctx->sub_state == wrap_handle_sub_state_running_cmd) {
		bool error = false;

		switch (run_cmd_collect(&ctx->cmd_ctx)) {
		case run_cmd_running: return true;
		case run_cmd_error: {
			error = true;
			break;
		}
		case run_cmd_finished: {
			ctx->sub_state = wrap_handle_sub_state_running;
			break;
		}
		}

		if (!wrap_run_cmd_finish(wk, ctx, error)) {
			return false;
		}
	} else if (ctx->sub_state == wrap_handle_sub_state_fetching) {
		if (ctx->opts.block) {
			mc_wait(1000);
		}

		struct mc_fetch_stats fetch_stats = { 0 };
		switch (mc_fetch_collect(ctx->fetch_ctx.handle, &fetch_stats)) {
		case mc_fetch_collect_result_pending: {
			ctx->fetch_ctx.downloaded = fetch_stats.downloaded;
			ctx->fetch_ctx.total = fetch_stats.total;
			return true;
		}
		case mc_fetch_collect_result_error:
			if (ctx->fetch_ctx.buf) {
				z_free(ctx->fetch_ctx.buf);
			}
			return false;
		case mc_fetch_collect_result_done: {
			ctx->sub_state = wrap_handle_sub_state_extracting;

			if (ctx->fetch_ctx.hash) {
				if (!wrap_checksum(wk,
					    ctx,
					    (const uint8_t *)ctx->fetch_ctx.buf,
					    ctx->fetch_ctx.len,
					    ctx->fetch_ctx.hash)) {
					return false;
				}

				TSTR(cache_path);
				path_join(wk, &cache_path, ctx->opts.subprojects, "packagecache");
				if (!fs_mkdir_p(cache_path.buf)) {
					return false;
				}
				path_push(wk, &cache_path, ctx->fetch_ctx.filename);

				if (!fs_write(
					    cache_path.buf, (const uint8_t *)ctx->fetch_ctx.buf, ctx->fetch_ctx.len)) {
					return false;
				}
			}
			return true;
		}
		}
	} else if (ctx->sub_state == wrap_handle_sub_state_extracting) {
		if (!muon_archive_extract(
			    (const char *)ctx->fetch_ctx.buf, ctx->fetch_ctx.len, ctx->fetch_ctx.dest_dir)) {
			return false;
		}
		ctx->sub_state = wrap_handle_sub_state_running;
	}

	switch (ctx->state) {
	case wrap_handle_state_init: {
		ctx->sub_state = wrap_handle_sub_state_running;
		timer_start(&ctx->duration);

		if (!wrap_parse(wk, ctx->opts.subprojects, wrap_file, &ctx->wrap)) {
			return false;
		}

		uint32_t i;
		for (i = 0; i < ARRAY_LEN(ctx->bufs); ++i) {
			tstr_init(&ctx->bufs[i], ctx->tstr_buf[i], ARRAY_LEN(ctx->tstr_buf[0]), 0);
		}

		return wrap_handle_default(wk, ctx);
	}
	case wrap_handle_state_check_dirty: {
		if (!fs_dir_exists(ctx->wrap.dest_dir.buf)) {
			wrap_handle_check_dirty_next_state(wk, ctx);
			ctx->wrap.outdated = true;
			return true;
		}

		switch (ctx->wrap.type) {
		case wrap_type_file: {
			if (ctx->wrap.fields[wf_source_url]) {
				ctx->wrap.outdated = true;

				TSTR(hash_path);
				path_join(wk, &hash_path, ctx->wrap.dest_dir.buf, meson_subproject_wrap_hash_txt);

				if (fs_file_exists(hash_path.buf)) {
					char buf[65] = { 0 };
					if (!wrap_hash(wrap_file, buf)) {
						return false;
					}

					struct source src;
					if (fs_read_entire_file(hash_path.buf, &src)) {
						if (src.len >= 64 && memcmp(buf, src.src, 64) == 0) {
							ctx->wrap.outdated = false;
						}
					}
				}
			}

			wrap_handle_check_dirty_next_state(wk, ctx);
			return true;
		}
		case wrap_type_git: {
			if (!is_git_dir(wk, ctx->wrap.dest_dir.buf)) {
				wrap_handle_check_dirty_next_state(wk, ctx);
				ctx->wrap.outdated = true;
				return true;
			}

			if (!wrap_run_cmd(wk,
				    ctx,
				    ARGV("git", "diff", "--quiet"),
				    ctx->wrap.dest_dir.buf,
				    wrap_run_cmd_flag_yield | wrap_run_cmd_flag_allow_failure)) {
				return false;
			}

			wrap_set_state(ctx, wrap_handle_state_check_dirty_git_parse_head);
			return true;
		}
		default: UNREACHABLE;
		}
	}
	case wrap_handle_state_check_dirty_git_parse_head: {
		ctx->wrap.dirty = ctx->cmd_ctx.status != 0;
		ctx->wrap.outdated = true;

		if (str_eqli(&STR("HEAD"), &STRL(ctx->wrap.fields[wf_revision]))) {
			// head is always outdated
			wrap_handle_check_dirty_next_state(wk, ctx);
			return true;
		}

		if (!wrap_git_rev_parse(wk, ctx, ctx->wrap.dest_dir.buf, "HEAD", &ctx->bufs[0])) {
			wrap_handle_check_dirty_next_state(wk, ctx);
			return true;
		}

		wrap_set_state(ctx, wrap_handle_state_check_dirty_git_parse_rev);
		return true;
	}
	case wrap_handle_state_check_dirty_git_parse_rev: {
		if (!wrap_git_rev_parse(
			    wk, ctx, ctx->wrap.dest_dir.buf, ctx->wrap.fields[wf_revision], &ctx->bufs[1])) {
			wrap_handle_check_dirty_next_state(wk, ctx);
			return true;
		}

		wrap_set_state(ctx, wrap_handle_state_check_dirty_git_cmp_rev);
		return true;
	}
	case wrap_handle_state_check_dirty_git_cmp_rev: {
		const struct tstr *head_rev = &ctx->bufs[0], *wrap_rev = &ctx->bufs[1];

		if (head_rev->len == wrap_rev->len && memcmp(head_rev->buf, wrap_rev->buf, head_rev->len) == 0) {
			ctx->wrap.outdated = false;
		}

		wrap_handle_check_dirty_next_state(wk, ctx);
		return true;
	}
	case wrap_handle_state_update: {
		if (!ctx->wrap.outdated) {
			wrap_set_state(ctx, wrap_handle_state_apply_patch);
			return true;
		}

		if (ctx->wrap.dirty) {
			wrap_log(ctx,
				log_warn,
				"cannot safely update outdated %s because it is dirty",
				ctx->wrap.dest_dir.buf);
			wrap_set_state(ctx, wrap_handle_state_apply_patch);
			return true;
		}

		ctx->opts.force_update = true;
		ctx->opts.mode = wrap_handle_mode_default;
		return wrap_handle_default(wk, ctx);
	}
	case wrap_handle_state_file_init: {
		return wrap_handle_file(wk, ctx);
	}
	case wrap_handle_state_git_init:
	case wrap_handle_state_git_fetch:
	case wrap_handle_state_git_fetch_fallback:
	case wrap_handle_state_git_checkout:
	case wrap_handle_state_git_done: {
		return wrap_handle_git(wk, ctx);
	}
	case wrap_handle_state_apply_patch: {
		if (ctx->wrap.fields[wf_patch_directory] || ctx->wrap.fields[wf_patch_filename]) {
			if (!wrap_apply_patch(wk, ctx)) {
				return false;
			}
		}

		if (ctx->wrap.updated && ctx->wrap.type == wrap_type_file) {
			char buf[66] = { 0 };
			if (!wrap_hash(wrap_file, buf)) {
				return false;
			}

			buf[65] = '\n';

			TSTR(hash_path);
			path_join(wk, &hash_path, ctx->wrap.dest_dir.buf, meson_subproject_wrap_hash_txt);

			if (!fs_write(hash_path.buf, (uint8_t *)buf, 64)) {
				return false;
			}
		}

		wrap_set_state(ctx, wrap_handle_state_done);
		return true;
	}
	case wrap_handle_state_done: {
		ctx->sub_state = wrap_handle_sub_state_complete;
		return true;
	}
	default: UNREACHABLE_RETURN;
	}
}

bool
wrap_handle(struct workspace *wk, const char *wrap_file, struct wrap_handle_ctx *ctx)
{
	ctx->opts.block = true;
	wrap_handle_async_start(wk);

	bool ok = true;

	while (ctx->sub_state != wrap_handle_sub_state_complete) {
		if (!wrap_handle_async(wk, wrap_file, ctx)) {
			ok = false;
			break;
		}
	}

	wrap_handle_async_end(wk);

	ctx->sub_state = wrap_handle_sub_state_collected;
	return ok;
}

struct wrap_load_all_ctx {
	struct workspace *wk;
	const char *subprojects;
	struct tstr *path;
};

static enum iteration_result
wrap_load_all_iter(void *_ctx, const char *file)
{
	struct wrap_load_all_ctx *ctx = _ctx;

	if (!str_endswith(&STRL(file), &STR(".wrap"))) {
		return ir_cont;
	}

	path_join(ctx->wk, ctx->path, ctx->subprojects, file);

	if (!fs_file_exists(ctx->path->buf)) {
		return ir_cont;
	}

	struct wrap wrap = { 0 };
	if (!wrap_parse(ctx->wk, ctx->subprojects, ctx->path->buf, &wrap)) {
		return ir_err;
	}

	// Add this wrap file as a regenerate dependency
	workspace_add_regenerate_dep(ctx->wk, make_str(ctx->wk, ctx->path->buf));

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

	wp_ctx.wrap_name_arr = make_obj(ctx->wk, obj_array);
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
	TSTR(wrap_path_buf);

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
