#include "posix.h"

#include "functions/common.h"
#include "functions/modules/fs.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"

static bool
func_exists(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_bool)->dat.boolean = fs_exists(wk_objstr(wk, an[0].val));
	return true;
}

const struct func_impl_name impl_tbl_module_fs[] = {
	{ "exists", func_exists },
	{ NULL, NULL },
};
