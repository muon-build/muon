#include "posix.h"

#include "functions/modules/sourceset.h"

static bool
func_module_sourceset_source_set(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_string | tc_file }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	return true;
}

const struct func_impl_name impl_tbl_module_sourceset[] = {
	{ "source_set", func_module_sourceset_source_set, },
	{ NULL, NULL },
};

