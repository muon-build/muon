/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Eli Schwartz <eschwartz@archlinux.org>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "error.h"
#include "formats/editorconfig.h"
#include "functions/both_libs.h"
#include "functions/kernel/custom_target.h"
#include "functions/modules/fs.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/os.h"
#include "platform/path.h"
#include "sha_256.h"

enum fix_file_path_opts {
	fix_file_path_noexpanduser = 1 << 0,
	fix_file_path_noabs = 1 << 1,
	fix_file_path_keep_hyphen = 1 << 2,
};

static const char *
fs_coerce_file_path(struct workspace *wk, uint32_t node, obj o, bool abs_build_target)
{
	enum obj_type t = get_obj_type(wk, o);
	const struct str *ss;
	switch (t) {
	case obj_string: ss = get_str(wk, o); break;
	case obj_file: ss = get_str(wk, *get_obj_file(wk, o)); break;
	case obj_both_libs: o = decay_both_libs(wk, o);
	/* fallthrough */
	case obj_build_target: {
		struct obj_build_target *tgt = get_obj_build_target(wk, o);
		const char *name = get_cstr(wk, tgt->build_name);
		if (!abs_build_target) {
			return name;
		}

		TSTR(joined);
		path_join(wk, &joined, get_cstr(wk, tgt->build_dir), name);
		return get_cstr(wk, tstr_into_str(wk, &joined));
	}
	case obj_custom_target: {
		o = get_obj_custom_target(wk, o)->output;
		obj res;
		if (!obj_array_flatten_one(wk, o, &res)) {
			vm_error_at(wk, node, "couldn't get path for custom target with multiple outputs");
			return false;
		}
		return get_file_path(wk, res);
	}
	default: UNREACHABLE;
	}

	if (str_has_null(ss)) {
		vm_error_at(wk, node, "path cannot contain null bytes");
		return 0;
	} else if (!ss->len) {
		vm_error_at(wk, node, "path cannot be empty");
		return 0;
	}

	return ss->s;
}

static bool
fix_file_path(struct workspace *wk, uint32_t err_node, obj path, enum fix_file_path_opts opts, struct tstr *buf)
{
	const char *s = fs_coerce_file_path(wk, err_node, path, false);
	if (!s) {
		return false;
	}

	if ((opts & fix_file_path_keep_hyphen) && strcmp(s, "-") == 0) {
		tstr_push(wk, buf, '-');
		return true;
	}

	if (path_is_absolute(s)) {
		path_copy(wk, buf, s);
	} else {
		if (!(opts & fix_file_path_noexpanduser) && s[0] == '~') {
			const char *home;
			if (!(home = fs_user_home())) {
				vm_error_at(wk, err_node, "failed to get user home directory");
				return false;
			}

			if (s[1] == '/' && s[2]) {
				path_join(wk, buf, home, &s[2]);
			} else {
				path_copy(wk, buf, home);
			}
		} else if (opts & fix_file_path_noabs) {
			path_copy(wk, buf, s);
		} else {
			path_join(wk, buf, workspace_cwd(wk), s);
		}
	}

	_path_normalize(wk, buf, true);
	return true;
}

typedef bool((*fs_lookup_func)(const char *));

static bool
func_module_fs_lookup_common(struct workspace *wk,
	obj *res,
	fs_lookup_func lookup,
	enum fix_file_path_opts opts,
	bool allow_file)
{
	type_tag t = tc_string;
	if (allow_file) {
		t |= tc_file;
	}

	struct args_norm an[] = { { t }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, opts, &path)) {
		return false;
	}

	*res = make_obj_bool(wk, lookup(path.buf));
	return true;
}

FUNC_IMPL(module_fs, exists, tc_bool, func_impl_flag_impure, .desc = "Check if a file or directory exists" )
{
	return func_module_fs_lookup_common(wk, res, fs_exists, 0, false);
}

FUNC_IMPL(module_fs, is_file, tc_bool, func_impl_flag_impure, .desc = "Check if a file exists" )
{
	return func_module_fs_lookup_common(wk, res, fs_file_exists, 0, false);
}

FUNC_IMPL(module_fs, is_dir, tc_bool, func_impl_flag_impure, .desc = "Check if a directory exists" )
{
	return func_module_fs_lookup_common(wk, res, fs_dir_exists, 0, false);
}

FUNC_IMPL(module_fs, is_symlink, tc_bool, func_impl_flag_impure, .desc = "Check if a symlink exists" )
{
	return func_module_fs_lookup_common(wk, res, fs_symlink_exists, 0, true);
}

FUNC_IMPL(module_fs, parent, tc_string, .desc = "Get the dirname of a path" )
{
	struct args_norm an[] = { { tc_coercible_files }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_noabs, &path)) {
		return false;
	}

	TSTR(buf);
	path_dirname(wk, &buf, path.buf);
	*res = tstr_into_str(wk, &buf);
	return true;
}

FUNC_IMPL(module_fs, read, tc_string, func_impl_flag_impure, .desc = "Read a file from disk" )
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	enum {
		kw_encoding,
	};
	struct args_kw akw[] = {
		[kw_encoding] = { "encoding", obj_string },
		0,
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (akw[kw_encoding].set) {
		if (!str_eql(get_str(wk, akw[kw_encoding].val), &STR("utf-8"))) {
			vm_error_at(wk, akw[kw_encoding].node, "only 'utf-8' supported");
		}
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_keep_hyphen, &path)) {
		return false;
	}

	struct source src = { 0 };
	if (!fs_read_entire_file(wk->a_scratch, path.buf, &src)) {
		return false;
	}

	*res = make_strn(wk, src.src, src.len);
	return true;
}

FUNC_IMPL(module_fs, is_absolute, tc_bool, .desc = "Check if a path is absolute" )
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	// TODO: Handle this
	*res = make_obj_bool(wk, path_is_absolute(get_cstr(wk, an[0].val)));
	return true;
}

FUNC_IMPL(module_fs, expanduser, tc_string, func_impl_flag_impure, .desc = "Expand a path that starts with a ~ into an absolute path"  )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	*res = tstr_into_str(wk, &path);
	return true;
}

FUNC_IMPL(module_fs, name, tc_string, .desc = "Get the basename of a path" )
{
	struct args_norm an[] = { { tc_coercible_files }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_noexpanduser, &path)) {
		return false;
	}

	TSTR(basename);
	path_basename(wk, &basename, path.buf);
	*res = tstr_into_str(wk, &basename);
	return true;
}

FUNC_IMPL(module_fs, stem, tc_string, .desc = "Get the basename of a path with its extension removed" )
{
	struct args_norm an[] = { { tc_coercible_files }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_noexpanduser, &path)) {
		return false;
	}

	TSTR(basename);
	path_basename(wk, &basename, path.buf);

	char *dot;
	if ((dot = strrchr(basename.buf, '.'))) {
		*dot = 0;
		basename.len = strlen(basename.buf);
	}

	*res = tstr_into_str(wk, &basename);
	return true;
}

FUNC_IMPL(module_fs, as_posix, tc_string, .desc = "Convert backslashes to `/`" )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const char *path = get_cstr(wk, an[0].val), *p;

	TSTR(buf);
	for (p = path; *p; ++p) {
		if (*p == '\\') {
			tstr_push(wk, &buf, '/');
			if (*(p + 1) == '\\') {
				++p;
			}
		} else {
			tstr_push(wk, &buf, *p);
		}
	}

	*res = tstr_into_str(wk, &buf);
	return true;
}

FUNC_IMPL(module_fs, replace_suffix, tc_string, .desc = "Replace a path's suffix" )
{
	struct args_norm an[] = { { tc_coercible_files }, { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_noabs, &path)) {
		return false;
	}

	char *base = strrchr(path.buf, '/');
	char *dot;
	if ((dot = strrchr(path.buf, '.')) && dot > base) {
		*dot = 0;
		path.len = strlen(path.buf);
	}

	tstr_pushs(wk, &path, get_cstr(wk, an[1].val));
	*res = tstr_into_str(wk, &path);
	return true;
}

FUNC_IMPL(module_fs, hash, tc_string, func_impl_flag_impure, .desc = "Calculate the content hash of a file" )
{
	struct args_norm an[] = { { tc_string | tc_file }, { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	if (!str_eql(get_str(wk, an[1].val), &STR("sha256"))) {
		vm_error_at(wk, an[1].node, "only sha256 is supported");
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	struct source src = { 0 };
	if (!fs_read_entire_file(wk->a_scratch, path.buf, &src)) {
		return false;
	}

	uint8_t hash[32] = { 0 };
	calc_sha_256(hash, src.src, src.len); // TODO: other hash algos

	char buf[65] = { 0 };
	sha256_to_str(hash, buf);

	*res = make_str(wk, buf);

	return true;
}

FUNC_IMPL(module_fs, size, tc_number, func_impl_flag_impure, .desc = "Get a file's size in bytes" )
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	uint64_t size;
	FILE *f;
	if (!(f = fs_fopen(path.buf, "rb"))) {
		return false;
	} else if (!fs_fsize(f, &size)) {
		return false;
	} else if (!fs_fclose(f)) {
		return false;
	}

	assert(size < INT64_MAX);
	*res = make_obj(wk, obj_number);
	set_obj_number(wk, *res, size);
	return true;
}

FUNC_IMPL(module_fs, is_samepath, tc_bool, func_impl_flag_impure, .desc = "Check if two paths point to the same object" )
{
	struct args_norm an[] = { { tc_string | tc_file }, { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path1);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path1)) {
		return false;
	}

	TSTR(path2);
	if (!fix_file_path(wk, an[1].node, an[1].val, 0, &path2)) {
		return false;
	}

	// TODO: handle symlinks

	*res = make_obj_bool(wk, strcmp(path1.buf, path2.buf) == 0);
	return true;
}

FUNC_IMPL(module_fs, copyfile, tc_custom_target, func_impl_flag_impure, .desc = "Creates a custom target that copies a file at build time" )
{
	struct args_norm an[] = { { tc_string | tc_file }, { tc_string, .optional = true }, ARG_TYPE_NULL };
	enum {
		kw_install,
		kw_install_dir,
		kw_install_tag,
		kw_install_mode,
	};
	struct args_kw akw[] = {
		[kw_install] = { "install", obj_bool },
		[kw_install_dir] = { "install_dir", TYPE_TAG_LISTIFY | tc_string | tc_bool },
		[kw_install_tag] = { "install_tag", tc_string }, // TODO
		[kw_install_mode] = { "install_mode", tc_install_mode_kw },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	obj output;
	if (an[1].set) {
		output = an[1].val;
	} else {
		TSTR(dest);
		path_basename(wk, &dest, path.buf);
		output = tstr_into_str(wk, &dest);
	}

	obj command;
	command = make_obj(wk, obj_array);
	push_args_null_terminated(wk,
		command,
		(char *const[]){
			(char *)wk->argv0,
			"internal",
			"eval",
			"-e",
			"commands/copyfile.meson",
			"@INPUT@",
			"@OUTPUT@",
			NULL,
		});

	struct make_custom_target_opts opts = {
		.name = make_str(wk, "copyfile"),
		.input_node = an[0].node,
		.output_node = an[1].node,
		.input_orig = an[0].val,
		.output_orig = output,
		.output_dir = get_cstr(wk, current_project(wk)->build_dir),
		.command_orig = command,
	};

	if (!make_custom_target(wk, &opts, res)) {
		return false;
	}

	obj_array_push(wk, current_project(wk)->targets, *res);

	struct obj_custom_target *tgt = get_obj_custom_target(wk, *res);

	if (!install_custom_target(wk, tgt, &akw[kw_install], 0, akw[kw_install_dir].val, 0)) {
		return false;
	}

	return true;
}

FUNC_IMPL(module_fs, relative_to, tc_string, .desc = "Get a path relative to another path" )
{
	struct args_norm an[] = { { tc_coercible_files }, { tc_coercible_files }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const char *p1, *p2;

	if (!(p1 = fs_coerce_file_path(wk, an[0].node, an[0].val, true))) {
		return false;
	} else if (!(p2 = fs_coerce_file_path(wk, an[1].node, an[1].val, true))) {
		return false;
	}

	TSTR(b1);
	TSTR(b2);

	if (path_is_absolute(p1)) {
		path_copy(wk, &b1, p1);
	} else {
		path_join(wk, &b1, workspace_cwd(wk), p1);
	}

	if (path_is_absolute(p2)) {
		path_copy(wk, &b2, p2);
	} else {
		path_join(wk, &b2, workspace_cwd(wk), p2);
	}

	TSTR(path);
	path_relative_to(wk, &path, b2.buf, b1.buf);
	*res = tstr_into_str(wk, &path);
	return true;
}

FUNC_IMPL(module_fs, write, 0, func_impl_flag_impure | func_impl_flag_sandbox_disable, .desc = "Write a file" )
{
	struct args_norm an[] = { { tc_string | tc_file }, { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	const struct str *ss = get_str(wk, an[1].val);
	if (!fs_write(path.buf, (uint8_t *)ss->s, ss->len)) {
		return false;
	}
	return true;
}

FUNC_IMPL(module_fs, copy, 0, func_impl_flag_impure | func_impl_flag_sandbox_disable, .desc = "Copy a file to a destination at configure time" )
{
	struct args_norm an[] = { { tc_string | tc_file }, { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_force,
	};
	struct args_kw akw[] = {
		[kw_force] = { "force", obj_bool },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	bool force = akw[kw_force].set ? get_obj_bool(wk, akw[kw_force].val) : false;

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	if (!fs_copy_file(wk, path.buf, get_cstr(wk, an[1].val), force)) {
		return false;
	}
	return true;
}

FUNC_IMPL(module_fs, cwd, tc_string, func_impl_flag_impure, .desc = "Return the muon's current working directory" )
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	TSTR(cwd);
	path_copy_cwd(wk, &cwd);
	*res = tstr_into_str(wk, &cwd);
	return true;
}

FUNC_IMPL(module_fs, make_absolute, tc_string, func_impl_flag_impure, .desc = "Make a path absolute by prepending the current working directory" )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	path_make_absolute(wk, &path, get_cstr(wk, an[0].val));
	*res = tstr_into_str(wk, &path);
	return true;
}

FUNC_IMPL(module_fs, mkdir, 0, func_impl_flag_impure | func_impl_flag_sandbox_disable, .desc = "Create a directory" )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	enum kwargs { kw_make_parents };
	struct args_kw akw[] = {
		[kw_make_parents] = { "make_parents", obj_bool },
		0,
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (akw[kw_make_parents].set && get_obj_bool(wk, akw[kw_make_parents].val)) {
		return fs_mkdir_p(wk, get_cstr(wk, an[0].val));
	} else {
		return fs_mkdir(get_cstr(wk, an[0].val), true);
	}
}

FUNC_IMPL(module_fs, rmdir, func_impl_flag_impure | func_impl_flag_sandbox_disable, .desc = "Remove a directory" )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_recursive,
		kw_force,
	};
	struct args_kw akw[] = {
		[kw_recursive] = { "recursive", obj_bool },
		[kw_force] = { "force", obj_bool },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	bool recursive = akw[kw_recursive].set ? get_obj_bool(wk, akw[kw_recursive].val) : false;
	bool force = akw[kw_force].set ? get_obj_bool(wk, akw[kw_force].val) : false;

	if (recursive) {
		return fs_rmdir_recursive(wk, get_cstr(wk, an[0].val), force);
	} else {
		return fs_rmdir(get_cstr(wk, an[0].val), force);
	}
}

FUNC_IMPL(module_fs, is_basename, tc_bool, .desc = "Check if a path is a basename" )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, path_is_basename(get_cstr(wk, an[0].val)));
	return true;
}

FUNC_IMPL(module_fs, without_ext, tc_string, .desc = "Get a path with its extension removed" )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	path_without_ext(wk, &path, get_cstr(wk, an[0].val));
	*res = tstr_into_str(wk, &path);
	return true;
}

FUNC_IMPL(module_fs, is_subpath, tc_bool, .desc = "Check if a path is a subpath of another path" )
{
	struct args_norm an[] = { { tc_string }, { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, path_is_subpath(wk, get_cstr(wk, an[0].val), get_cstr(wk, an[1].val)));
	return true;
}

FUNC_IMPL(module_fs, add_suffix, tc_string, .desc = "Append a suffix to a path" )
{
	struct args_norm an[] = { { tc_string }, { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	path_copy(wk, &path, get_cstr(wk, an[0].val));
	tstr_pushs(wk, &path, get_cstr(wk, an[1].val));
	*res = tstr_into_str(wk, &path);
	return true;
}

FUNC_IMPL(module_fs, executable, tc_string, .desc = "Prepend ./ to a path if it is a basename" )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	path_executable(wk, &path, get_cstr(wk, an[0].val));
	*res = tstr_into_str(wk, &path);
	return true;
}

struct func_module_fs_glob_ctx {
	struct workspace *wk;
	const char *pat, *prefix, *matchprefix;
	enum {
		fs_glob_mode_normal,
		fs_glob_mode_recurse,
	} mode;
	obj res;
};

static enum iteration_result
func_module_fs_glob_cb(void *_ctx, const char *name)
{
	const struct func_module_fs_glob_ctx *ctx = _ctx;
	struct workspace *wk = ctx->wk;

	struct func_module_fs_glob_ctx subctx = *ctx;
	if (ctx->mode == fs_glob_mode_recurse) {
		goto recurse_all_mode;
	}

	struct str pat = { .s = ctx->pat };
	{
		uint32_t i;
		for (i = 0; pat.s[i] && pat.s[i] != '/'; ++i) {
			++pat.len;
		}
	}

	/* L("path: %s, pat: %.*s", name, pat.len, pat.s); */

	if (str_eql(&pat, &STR("**"))) {
		subctx.mode = fs_glob_mode_recurse;
		subctx.matchprefix = "/";
		goto recurse_all_mode;
	}

	if (str_eql_glob(&pat, &STRL(name))) {
		TSTR(path);
		path_join(wk, &path, ctx->prefix, name);
		subctx.prefix = path.buf;

		subctx.pat += pat.len;
		if (!*subctx.pat) {
			obj_array_push(wk, ctx->res, tstr_into_str(wk, &path));
		} else {
			++subctx.pat;
			if (fs_dir_exists(path.buf)) {
				if (!fs_dir_foreach(path.buf, &subctx, func_module_fs_glob_cb)) {
					return ir_err;
				}
			}
		}
	}

	return ir_cont;
recurse_all_mode: {
	TSTR(path);
	path_join(wk, &path, subctx.matchprefix, name);
	TSTR(full_path);
	path_join(wk, &full_path, subctx.prefix, name);

	subctx.prefix = full_path.buf;
	subctx.matchprefix = path.buf;

	/* L("recurse: path: %s, pat: %s", path.buf, ctx->pat); */

	if (editorconfig_pattern_match(ctx->pat, path.buf)) {
		obj_array_push(wk, ctx->res, tstr_into_str(wk, &full_path));
	}

	if (fs_dir_exists(full_path.buf)) {
		if (!fs_dir_foreach(full_path.buf, &subctx, func_module_fs_glob_cb)) {
			return ir_err;
		}
	}
	return ir_cont;
}
}

FUNC_IMPL(module_fs, glob, tc_array, func_impl_flag_impure, .desc = "Gather a list of files matching a glob expression" )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(pat);
	{
		const struct str *_pat = get_str(wk, an[0].val);
		if (str_has_null(_pat)) {
			vm_error(wk, "null byte not allowed in pattern");
			return false;
		}

		path_join(wk, &pat, workspace_cwd(wk), _pat->s);
	}

	TSTR(prefix);
	{
		uint32_t i, prefix_len = 0;
		for (i = 0; pat.buf[i]; ++i) {
			if (pat.buf[i] == '\\') {
				++i;
				continue;
			}

			if (pat.buf[i] == '*') {
				break;
			} else if (pat.buf[i] == '/') {
				prefix_len = i;
			}
		}

		tstr_pushn(wk, &prefix, pat.buf, prefix_len);
	}

	if (prefix.len) {
		pat.buf += prefix.len + 1;
		pat.len -= prefix.len + 1;
	} else {
		path_copy(wk, &prefix, ".");
	}

	/* L("prefix: %.*s, pat: %s", prefix.len, prefix.buf, pat.buf); */

	*res = make_obj(wk, obj_array);

	if (!fs_dir_exists(prefix.buf)) {
		vm_error(wk, "Path \"%s\" does not exist", prefix.buf);
		return false;
	}

	struct func_module_fs_glob_ctx ctx = {
		.wk = wk,
		.pat = pat.buf,
		.prefix = prefix.buf,
		.res = *res,
	};

	return fs_dir_foreach(prefix.buf, &ctx, func_module_fs_glob_cb);
}

struct delete_suffixes_ctx {
	struct workspace *wk;
	const char *base_dir;
	const char *suffix;
};

static enum iteration_result
delete_suffix_recursive(void *_ctx, const char *path)
{
	enum iteration_result ir_res = ir_err;
	struct delete_suffixes_ctx *ctx = _ctx;
	struct stat sb;

	TSTR(name);

	path_join(ctx->wk, &name, ctx->base_dir, path);

	// Check for existence first, before doing a stat. It's possible that
	// another process has deleted this file in the time since the iteration
	// machinery found this file. In this case, the file no longer exists, so
	// just continue.
	if (!fs_exists(name.buf)) {
		return ir_cont;
	}

	if (!fs_stat(name.buf, &sb)) {
		goto ret;
	}

	if (S_ISDIR(sb.st_mode)) {
		struct delete_suffixes_ctx new_ctx = *ctx;
		new_ctx.base_dir = name.buf;

		if (!fs_dir_foreach(new_ctx.base_dir, &new_ctx, delete_suffix_recursive)) {
			goto ret;
		}

	} else if (S_ISREG(sb.st_mode)) {
		if (fs_has_extension(name.buf, ctx->suffix) && !fs_remove(name.buf)) {
			goto ret;
		}

	} else {
		LOG_E("unhandled file type: %s", name.buf);
		goto ret;
	}

	ir_res = ir_cont;

ret:
	return ir_res;
}

FUNC_IMPL(module_fs, delete_with_suffix, func_impl_flag_impure | func_impl_flag_sandbox_disable, .desc = "Recursively delete all files with a matching suffix" )
{
	struct args_norm an[] = { { tc_string }, { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const char *base_dir = get_cstr(wk, an[0].val);
	struct delete_suffixes_ctx ctx = {
		.wk = wk,
		.base_dir = base_dir,
		.suffix = get_cstr(wk, an[1].val),
	};

	return fs_dir_foreach(base_dir, &ctx, delete_suffix_recursive);
}

FUNC_IMPL(module_fs, canonicalize, tc_string, .desc = "Make a path absolute and replace .."  )
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	TSTR(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_noexpanduser, &path)) {
		return false;
	}

	*res = tstr_into_str(wk, &path);
	return true;
}

FUNC_REGISTER(module_fs)
{
	FUNC_IMPL_REGISTER(module_fs, as_posix);
	FUNC_IMPL_REGISTER(module_fs, copyfile);
	FUNC_IMPL_REGISTER(module_fs, exists);
	FUNC_IMPL_REGISTER(module_fs, expanduser);
	FUNC_IMPL_REGISTER(module_fs, hash);
	FUNC_IMPL_REGISTER(module_fs, is_absolute);
	FUNC_IMPL_REGISTER(module_fs, is_dir);
	FUNC_IMPL_REGISTER(module_fs, is_file);
	FUNC_IMPL_REGISTER(module_fs, is_samepath);
	FUNC_IMPL_REGISTER(module_fs, is_symlink);
	FUNC_IMPL_REGISTER(module_fs, name);
	FUNC_IMPL_REGISTER(module_fs, parent);
	FUNC_IMPL_REGISTER(module_fs, read);
	FUNC_IMPL_REGISTER(module_fs, relative_to);
	FUNC_IMPL_REGISTER(module_fs, replace_suffix);
	FUNC_IMPL_REGISTER(module_fs, size);
	FUNC_IMPL_REGISTER(module_fs, stem);

	// non-standard muon extensions
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_fs, add_suffix);
		FUNC_IMPL_REGISTER(module_fs, copy);
		FUNC_IMPL_REGISTER(module_fs, cwd);
		FUNC_IMPL_REGISTER(module_fs, executable);
		FUNC_IMPL_REGISTER(module_fs, glob);
		FUNC_IMPL_REGISTER(module_fs, is_basename);
		FUNC_IMPL_REGISTER(module_fs, is_subpath);
		FUNC_IMPL_REGISTER(module_fs, make_absolute);
		FUNC_IMPL_REGISTER(module_fs, mkdir);
		FUNC_IMPL_REGISTER(module_fs, rmdir);
		FUNC_IMPL_REGISTER(module_fs, without_ext);
		FUNC_IMPL_REGISTER(module_fs, write);
		FUNC_IMPL_REGISTER(module_fs, delete_with_suffix);
		FUNC_IMPL_REGISTER(module_fs, canonicalize);
	}
}
