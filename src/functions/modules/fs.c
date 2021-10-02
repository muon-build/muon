#include "posix.h"

#include "functions/common.h"
#include "functions/modules/fs.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static bool
func_module_fs_exists(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = fs_exists(get_cstr(wk, an[0].val));
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

const struct func_impl_name impl_tbl_module_fs[] = {
	{ "exists", func_module_fs_exists },
	{ "parent", func_module_fs_parent },
	{ NULL, NULL },
};
