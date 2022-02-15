#include "posix.h"

#include "functions/common.h"
#include "functions/file.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_file_full_path(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = *get_obj_file(wk, rcvr);
	return true;
}

const struct func_impl_name impl_tbl_file[] = {
	{ "full_path", func_file_full_path },
	{ NULL, NULL },
};
