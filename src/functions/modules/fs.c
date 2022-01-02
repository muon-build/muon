#include "posix.h"

#include "functions/common.h"
#include "functions/modules/fs.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static bool
fix_file_path(struct workspace *wk, uint32_t err_node, obj path, const char **res)
{
	const struct str *ss = get_str(wk, path);
	if (str_has_null(ss)) {
		interp_error(wk, err_node, "path cannot contain null bytes");
		return false;
	}

	if (path_is_absolute(ss->s)) {
		*res = ss->s;
	} else {
		static char buf[PATH_MAX];
		if (!path_join(buf, PATH_MAX, get_cstr(wk, current_project(wk)->cwd), ss->s )) {
			return false;
		}

		*res = buf;
	}

	return true;
}

typedef bool ((*fs_lookup_func)(const char *));

static bool
func_module_fs_lookup_common(struct workspace *wk, uint32_t args_node, obj *res, fs_lookup_func lookup)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *path;
	if (!fix_file_path(wk, an[0].node, an[0].val, &path)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = lookup(path);
	return true;
}

static bool
func_module_fs_exists(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return func_module_fs_lookup_common(wk, args_node, res, fs_exists);
}

static bool
func_module_fs_is_file(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return func_module_fs_lookup_common(wk, args_node, res, fs_file_exists);
}

static bool
func_module_fs_is_dir(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return func_module_fs_lookup_common(wk, args_node, res, fs_dir_exists);
}

static bool
func_module_fs_is_symlink(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_any }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	struct obj *arg1 = get_obj(wk, an[0].val);

	obj pathobj;
	switch (arg1->type) {
	case obj_string:
		pathobj = an[0].val;
		break;
	case obj_file:
		pathobj = arg1->dat.file;
		break;
	default:
		interp_error(wk, an[0].node, "expected string or file, got %s", obj_type_to_s(arg1->type));
		return false;
	}

	const char *path;
	if (!fix_file_path(wk, an[0].node, pathobj, &path)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = fs_symlink_exists(path);
	return true;
}

static bool
func_module_fs_parent(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	char buf[PATH_MAX];
	if (!path_dirname(buf, PATH_MAX, get_cstr(wk, an[0].val))) {
		return false;
	}

	*res = make_str(wk, buf);

	return true;
}

static bool
func_module_fs_read(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	struct source src = { 0 };
	if (!fs_read_entire_file(get_cstr(wk, an[0].val), &src)) {
		return false;
	}

	*res = make_strn(wk, src.src, src.len);

	fs_source_destroy(&src);
	return true;
}

static bool
func_module_fs_write(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const struct str *ss = get_str(wk, an[1].val);
	if (!fs_write(get_cstr(wk, an[0].val), (uint8_t *)ss->s, ss->len)) {
		return false;
	}
	return true;
}

static bool
func_module_fs_is_absolute(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = path_is_absolute(get_cstr(wk, an[0].val));
	return true;
}

const struct func_impl_name impl_tbl_module_fs[] = {
	{ "exists", func_module_fs_exists },
	{ "is_absolute", func_module_fs_is_absolute },
	{ "is_dir", func_module_fs_is_dir },
	{ "is_file", func_module_fs_is_file },
	{ "is_symlink", func_module_fs_is_symlink },
	{ "parent", func_module_fs_parent },
	{ "read", func_module_fs_read },
	{ "write", func_module_fs_write },
	{ NULL, NULL },
};
