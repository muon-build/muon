#include "posix.h"

#include "functions/common.h"
#include "functions/modules/fs.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

enum fix_file_path_opts {
	fix_file_path_allow_file = 1 << 0,
	fix_file_path_expanduser = 1 << 1,
};

static bool
fix_file_path(struct workspace *wk, uint32_t err_node, obj path, enum fix_file_path_opts opts, const char **res)
{
	enum obj_type t = get_obj_type(wk, path);
	const struct str *ss;
	switch (t) {
	case obj_string:
		ss = get_str(wk, path);
		break;
	case obj_file:
		if (opts & fix_file_path_allow_file) {
			ss = get_str(wk, *get_obj_file(wk, path));
			break;
		}
	// FALLTHROUGH
	default:
		interp_error(wk, err_node, "expected string%s, got %s",
			(opts & fix_file_path_allow_file) ? " or file" : "",
			obj_type_to_s(t));
		return false;
	}

	if (str_has_null(ss)) {
		interp_error(wk, err_node, "path cannot contain null bytes");
		return false;
	} else if (!ss->len) {
		interp_error(wk, err_node, "path cannot be empty");
		return false;
	}

	if (path_is_absolute(ss->s)) {
		*res = ss->s;
	} else {
		static char buf[PATH_MAX];

		if ((opts & fix_file_path_expanduser) && ss->s[0] == '~') {
			const char *home;
			if (!(home = fs_user_home())) {
				interp_error(wk, err_node, "failed to get user home directory");
				return false;
			}

			if (!path_join(buf, PATH_MAX, home, &ss->s[1])) {
				return false;
			}
		} else {
			if (!path_join(buf, PATH_MAX, get_cstr(wk, current_project(wk)->cwd), ss->s)) {
				return false;
			}
		}

		*res = buf;
	}

	return true;
}

typedef bool ((*fs_lookup_func)(const char *));

static bool
func_module_fs_lookup_common(struct workspace *wk, uint32_t args_node, obj *res, fs_lookup_func lookup, enum fix_file_path_opts opts)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *path;
	if (!fix_file_path(wk, an[0].node, an[0].val, opts, &path)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, lookup(path));
	return true;
}

static bool
func_module_fs_exists(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return func_module_fs_lookup_common(wk, args_node, res, fs_exists, fix_file_path_expanduser);
}

static bool
func_module_fs_is_file(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return func_module_fs_lookup_common(wk, args_node, res, fs_file_exists, fix_file_path_expanduser);
}

static bool
func_module_fs_is_dir(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return func_module_fs_lookup_common(wk, args_node, res, fs_dir_exists, fix_file_path_expanduser);
}

static bool
func_module_fs_is_symlink(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return func_module_fs_lookup_common(wk, args_node, res, fs_symlink_exists,
		fix_file_path_allow_file | fix_file_path_expanduser);
}

static bool
func_module_fs_parent(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *path;
	if (!fix_file_path(wk, an[0].node, an[0].val,
		fix_file_path_allow_file | fix_file_path_expanduser, &path)) {
		return false;
	}

	char buf[PATH_MAX];
	if (!path_dirname(buf, PATH_MAX, path)) {
		return false;
	}

	*res = make_str(wk, buf);

	return true;
}

static bool
func_module_fs_read(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *path;
	if (!fix_file_path(wk, an[0].node, an[0].val,
		fix_file_path_allow_file | fix_file_path_expanduser, &path)) {
		return false;
	}

	struct source src = { 0 };
	if (!fs_read_entire_file(path, &src)) {
		return false;
	}

	*res = make_strn(wk, src.src, src.len);

	fs_source_destroy(&src);
	return true;
}

static bool
func_module_fs_write(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *path;
	if (!fix_file_path(wk, an[0].node, an[0].val,
		fix_file_path_allow_file | fix_file_path_expanduser, &path)) {
		return false;
	}

	const struct str *ss = get_str(wk, an[1].val);
	if (!fs_write(path, (uint8_t *)ss->s, ss->len)) {
		return false;
	}
	return true;
}

static bool
func_module_fs_is_absolute(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *path;
	if (!fix_file_path(wk, an[0].node, an[0].val, 0, &path)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, path_is_absolute(path));
	return true;
}

static bool
func_module_fs_expanduser(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *path;
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_expanduser, &path)) {
		return false;
	}

	*res = make_str(wk, path);
	return true;
}

static bool
func_module_fs_name(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *path;
	if (!fix_file_path(wk, an[0].node, an[0].val, fix_file_path_allow_file, &path)) {
		return false;
	}

	char basename[PATH_MAX];
	if (!path_basename(basename, PATH_MAX, path)) {
		return false;
	}

	*res = make_str(wk, basename);
	return true;
}

const struct func_impl_name impl_tbl_module_fs[] = {
	{ "exists", func_module_fs_exists, tc_bool },
	{ "expanduser", func_module_fs_expanduser, tc_string },
	{ "is_absolute", func_module_fs_is_absolute, tc_bool },
	{ "is_dir", func_module_fs_is_dir, tc_bool },
	{ "is_file", func_module_fs_is_file, tc_bool },
	{ "is_symlink", func_module_fs_is_symlink, tc_bool },
	{ "name", func_module_fs_name, tc_string },
	{ "parent", func_module_fs_parent, tc_string },
	{ "read", func_module_fs_read, tc_string },
	{ "write", func_module_fs_write }, // muon extension
	{ NULL, NULL },
};
