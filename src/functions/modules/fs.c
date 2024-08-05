/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Eli Schwartz <eschwartz@archlinux.org>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "coerce.h"
#include "error.h"
#include "functions/kernel/custom_target.h"
#include "functions/modules/fs.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "sha_256.h"

enum fix_file_path_opts {
	fix_file_path_noexpanduser = 1 << 0,
	fix_file_path_noabs = 1 << 1,
};

static const char *
fs_coerce_file_path(struct workspace *wk, uint32_t node, obj o, bool abs_build_target)
{
	enum obj_type t = get_obj_type(wk, o);
	const struct str *ss;
	switch (t) {
	case obj_string: ss = get_str(wk, o); break;
	case obj_file: ss = get_str(wk, *get_obj_file(wk, o)); break;
	case obj_both_libs: o = get_obj_both_libs(wk, o)->dynamic_lib;
	/* fallthrough */
	case obj_build_target: {
		struct obj_build_target *tgt = get_obj_build_target(wk, o);
		const char *name = get_cstr(wk, tgt->build_name);
		if (!abs_build_target) {
			return name;
		}

		SBUF(joined);
		path_join(wk, &joined, get_cstr(wk, tgt->build_dir), name);
		return get_cstr(wk, sbuf_into_str(wk, &joined));
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
fix_file_path(struct workspace *wk, uint32_t err_node, obj path, enum fix_file_path_opts opts, struct sbuf *buf)
{
	const char *s = fs_coerce_file_path(wk, err_node, path, false);
	if (!s) {
		return false;
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

			path_join(wk, buf, home, &s[1]);
		} else if (opts & fix_file_path_noabs) {
			path_copy(wk, buf, s);
		} else {
			struct project *proj = current_project(wk);
			path_join(wk, buf, proj ? get_cstr(wk, proj->cwd) : "", s);
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

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, opts, &path)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, lookup(path.buf));
	return true;
}

static bool
func_module_fs_exists(struct workspace *wk, obj self, obj *res)
{
	return func_module_fs_lookup_common(wk, res, fs_exists, 0, false);
}

static bool
func_module_fs_is_file(struct workspace *wk, obj self, obj *res)
{
	return func_module_fs_lookup_common(wk, res, fs_file_exists, 0, false);
}

static bool
func_module_fs_is_dir(struct workspace *wk, obj self, obj *res)
{
	return func_module_fs_lookup_common(wk, res, fs_dir_exists, 0, false);
}

static bool
func_module_fs_is_symlink(struct workspace *wk, obj self, obj *res)
{
	return func_module_fs_lookup_common(wk, res, fs_symlink_exists, 0, true);
}

static bool
func_module_fs_parent(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_coercible_files }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_noabs, &path)) {
		return false;
	}

	SBUF(buf);
	path_dirname(wk, &buf, path.buf);
	*res = sbuf_into_str(wk, &buf);
	return true;
}

static bool
func_module_fs_read(struct workspace *wk, obj self, obj *res)
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
		if (!str_eql(get_str(wk, akw[kw_encoding].val), &WKSTR("utf-8"))) {
			vm_error_at(wk, akw[kw_encoding].node, "only 'utf-8' supported");
		}
	}

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	struct source src = { 0 };
	if (!fs_read_entire_file(path.buf, &src)) {
		return false;
	}

	*res = make_strn(wk, src.src, src.len);

	fs_source_destroy(&src);
	return true;
}

static bool
func_module_fs_is_absolute(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	// TODO: Handle this
	set_obj_bool(wk, *res, path_is_absolute(get_cstr(wk, an[0].val)));
	return true;
}

static bool
func_module_fs_expanduser(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	*res = sbuf_into_str(wk, &path);
	return true;
}

static bool
func_module_fs_name(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_coercible_files }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_noexpanduser, &path)) {
		return false;
	}

	SBUF(basename);
	path_basename(wk, &basename, path.buf);
	*res = sbuf_into_str(wk, &basename);
	return true;
}

static bool
func_module_fs_stem(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_coercible_files }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_noexpanduser, &path)) {
		return false;
	}

	SBUF(basename);
	path_basename(wk, &basename, path.buf);

	char *dot;
	if ((dot = strrchr(basename.buf, '.'))) {
		*dot = 0;
		basename.len = strlen(basename.buf);
	}

	*res = sbuf_into_str(wk, &basename);
	return true;
}

static bool
func_module_fs_as_posix(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	const char *path = get_cstr(wk, an[0].val), *p;

	SBUF(buf);
	for (p = path; *p; ++p) {
		if (*p == '\\') {
			sbuf_push(wk, &buf, '/');
			if (*(p + 1) == '\\') {
				++p;
			}
		} else {
			sbuf_push(wk, &buf, *p);
		}
	}

	*res = sbuf_into_str(wk, &buf);
	return true;
}

static bool
func_module_fs_replace_suffix(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_coercible_files }, { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_noabs, &path)) {
		return false;
	}

	char *base = strrchr(path.buf, '/');
	char *dot;
	if ((dot = strrchr(path.buf, '.')) && dot > base) {
		*dot = 0;
		path.len = strlen(path.buf);
	}

	sbuf_pushs(wk, &path, get_cstr(wk, an[1].val));
	*res = sbuf_into_str(wk, &path);
	return true;
}

static bool
func_module_fs_hash(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	if (!str_eql(get_str(wk, an[1].val), &WKSTR("sha256"))) {
		vm_error_at(wk, an[1].node, "only sha256 is supported");
		return false;
	}

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	struct source src = { 0 };
	if (!fs_read_entire_file(path.buf, &src)) {
		return false;
	}

	uint8_t hash[32] = { 0 };
	calc_sha_256(hash, src.src, src.len); // TODO: other hash algos

	char buf[65] = { 0 };
	uint32_t i, bufi = 0;
	for (i = 0; i < 32; ++i) {
		snprintf(&buf[bufi], 3, "%x", hash[i]);
		bufi += 2;
	}

	*res = make_str(wk, buf);

	fs_source_destroy(&src);
	return true;
}

static bool
func_module_fs_size(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
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
	make_obj(wk, res, obj_number);
	set_obj_number(wk, *res, size);
	return true;
}

static bool
func_module_fs_is_samepath(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path1);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path1)) {
		return false;
	}

	SBUF(path2);
	if (!fix_file_path(wk, an[1].node, an[1].val, 0, &path2)) {
		return false;
	}

	// TODO: handle symlinks

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, strcmp(path1.buf, path2.buf) == 0);
	return true;
}

static bool
func_module_fs_copyfile(struct workspace *wk, obj self, obj *res)
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

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	obj output;
	if (an[1].set) {
		output = an[1].val;
	} else {
		SBUF(dest);
		path_basename(wk, &dest, path.buf);
		output = sbuf_into_str(wk, &dest);
	}

	obj command;
	make_obj(wk, &command, obj_array);
	push_args_null_terminated(wk,
		command,
		(char *const[]){
			(char *)wk->argv0,
			"internal",
			"eval",
			"-e",
			"copyfile.meson",
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

	return true;
}

static bool
func_module_fs_relative_to(struct workspace *wk, obj self, obj *res)
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

	SBUF(b1);
	SBUF(b2);

	if (path_is_absolute(p1)) {
		path_copy(wk, &b1, p1);
	} else {
		path_join(wk, &b1, get_cstr(wk, current_project(wk)->cwd), p1);
	}

	if (path_is_absolute(p2)) {
		path_copy(wk, &b2, p2);
	} else {
		path_join(wk, &b2, get_cstr(wk, current_project(wk)->cwd), p2);
	}

	SBUF(path);
	path_relative_to(wk, &path, b2.buf, b1.buf);
	*res = sbuf_into_str(wk, &path);
	return true;
}

const struct func_impl impl_tbl_module_fs[] = {
	{ "as_posix", func_module_fs_as_posix, tc_string, true },
	{ "copyfile", func_module_fs_copyfile, tc_custom_target },
	{ "exists", func_module_fs_exists, tc_bool },
	{ "expanduser", func_module_fs_expanduser, tc_string },
	{ "hash", func_module_fs_hash, tc_string },
	{ "is_absolute", func_module_fs_is_absolute, tc_bool, true },
	{ "is_dir", func_module_fs_is_dir, tc_bool },
	{ "is_file", func_module_fs_is_file, tc_bool },
	{ "is_samepath", func_module_fs_is_samepath, tc_bool },
	{ "is_symlink", func_module_fs_is_symlink, tc_bool },
	{ "name", func_module_fs_name, tc_string, true },
	{ "parent", func_module_fs_parent, tc_string, true },
	{ "read", func_module_fs_read, tc_string },
	{ "relative_to", func_module_fs_relative_to, tc_string, true },
	{ "replace_suffix", func_module_fs_replace_suffix, tc_string, true },
	{ "size", func_module_fs_size, tc_number },
	{ "stem", func_module_fs_stem, tc_string, true },
	{ NULL, NULL },
};

static bool
func_module_fs_write(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	const struct str *ss = get_str(wk, an[1].val);
	if (!fs_write(path.buf, (uint8_t *)ss->s, ss->len)) {
		return false;
	}
	return true;
}

static bool
func_module_fs_copy(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, { obj_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	if (!fs_copy_file(path.buf, get_cstr(wk, an[1].val))) {
		return false;
	}
	return true;
}

static bool
func_module_fs_cwd(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	SBUF(cwd);
	path_copy_cwd(wk, &cwd);
	*res = sbuf_into_str(wk, &cwd);
	return true;
}

static bool
func_module_fs_make_absolute(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	path_make_absolute(wk, &path, get_cstr(wk, an[0].val));
	*res = sbuf_into_str(wk, &path);
	return true;
}

static bool
func_module_fs_mkdir(struct workspace *wk, obj self, obj *res)
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
		return fs_mkdir_p(get_cstr(wk, an[0].val));
	} else {
		return fs_mkdir(get_cstr(wk, an[0].val));
	}
}

static bool
func_module_fs_rmdir(struct workspace *wk, obj rcvr, obj *res)
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
		return fs_rmdir_recursive(get_cstr(wk, an[0].val), force);
	} else {
		return fs_rmdir(get_cstr(wk, an[0].val), force);
	}
}

static bool
func_module_fs_is_basename(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, path_is_basename(get_cstr(wk, an[0].val)));
	return true;
}

static bool
func_module_fs_without_ext(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	path_without_ext(wk, &path, get_cstr(wk, an[0].val));
	*res = sbuf_into_str(wk, &path);
	return true;
}

static bool
func_module_fs_is_subpath(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string }, { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, path_is_subpath(get_cstr(wk, an[0].val), get_cstr(wk, an[1].val)));
	return true;
}

static bool
func_module_fs_add_suffix(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string }, { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	path_copy(wk, &path, get_cstr(wk, an[0].val));
	sbuf_pushs(wk, &path, get_cstr(wk, an[1].val));
	*res = sbuf_into_str(wk, &path);
	return true;
}

static bool
func_module_fs_executable(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	SBUF(path);
	path_executable(wk, &path, get_cstr(wk, an[0].val));
	*res = sbuf_into_str(wk, &path);
	return true;
}

const struct func_impl impl_tbl_module_fs_internal[] = {
	{ "as_posix", func_module_fs_as_posix, tc_string, true },
	{ "copyfile", func_module_fs_copyfile },
	{ "exists", func_module_fs_exists, tc_bool },
	{ "expanduser", func_module_fs_expanduser, tc_string },
	{ "hash", func_module_fs_hash, tc_string },
	{ "is_absolute", func_module_fs_is_absolute, tc_bool, true },
	{ "is_dir", func_module_fs_is_dir, tc_bool },
	{ "is_file", func_module_fs_is_file, tc_bool },
	{ "is_samepath", func_module_fs_is_samepath, tc_bool },
	{ "is_symlink", func_module_fs_is_symlink, tc_bool },
	{ "name", func_module_fs_name, tc_string, true },
	{ "parent", func_module_fs_parent, tc_string, true },
	{ "read", func_module_fs_read, tc_string },
	{ "replace_suffix", func_module_fs_replace_suffix, tc_string, true },
	{ "size", func_module_fs_size, tc_number },
	{ "stem", func_module_fs_stem, tc_string, true },
	// non-standard muon extensions
	{ "add_suffix", func_module_fs_add_suffix, tc_string, true },
	{ "copy", func_module_fs_copy, .fuzz_unsafe = true },
	{ "cwd", func_module_fs_cwd, tc_string },
	{ "executable", func_module_fs_executable, tc_string, true },
	{ "is_basename", func_module_fs_is_basename, tc_bool, true },
	{ "is_subpath", func_module_fs_is_subpath, tc_bool, true },
	{ "make_absolute", func_module_fs_make_absolute, tc_string },
	{ "mkdir", func_module_fs_mkdir, .fuzz_unsafe = true },
	{ "rmdir", func_module_fs_rmdir, .fuzz_unsafe = true },
	{ "relative_to", func_module_fs_relative_to, tc_string, true },
	{ "without_ext", func_module_fs_without_ext, tc_string, true },
	{ "write", func_module_fs_write, .fuzz_unsafe = true },
	{ NULL, NULL },
};
