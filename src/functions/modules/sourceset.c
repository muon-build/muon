#include "posix.h"

#include "functions/modules/sourceset.h"

static bool
func_module_sourceset_source_set(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_source_set);
	return true;
}

const struct func_impl_name impl_tbl_module_sourceset[] = {
	{ "source_set", func_module_sourceset_source_set, tc_source_set, },
	{ NULL, NULL },
};

