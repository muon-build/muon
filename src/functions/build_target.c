#include "posix.h"

#include <string.h>

#include "functions/build_target.h"
#include "functions/common.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/path.h"

bool
tgt_build_path(struct workspace *wk, struct obj *tgt, bool relative, char res[PATH_MAX])
{
	char tmp[PATH_MAX] = { 0 };
	if (!path_join(tmp, PATH_MAX, get_cstr(wk, tgt->dat.tgt.build_dir), get_cstr(wk, tgt->dat.tgt.build_name))) {
		return false;
	}

	if (relative) {
		if (!path_relative_to(res, PATH_MAX, wk->build_root, tmp)) {
			return false;
		}
	} else {
		memcpy(res, tmp, PATH_MAX);
	}

	return true;
}

static bool
func_build_target_name(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = get_obj(wk, rcvr)->dat.tgt.name;
	return true;
}

static bool
func_build_target_full_path(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	struct obj *tgt = get_obj(wk, rcvr);

	char path[PATH_MAX];
	if (!tgt_build_path(wk, tgt, false, path)) {
		return false;
	}

	*obj = make_str(wk, path);
	return true;
}

const struct func_impl_name impl_tbl_build_target[] = {
	{ "name", func_build_target_name },
	{ "full_path", func_build_target_full_path },
	{ NULL, NULL },
};
