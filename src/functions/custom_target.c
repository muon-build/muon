#include "posix.h"

#include "functions/common.h"
#include "functions/custom_target.h"
#include "interpreter.h"
#include "log.h"

static bool
func_to_list(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = get_obj(wk, rcvr)->dat.custom_target.output;
	return true;
}

const struct func_impl_name impl_tbl_custom_target[] = {
	{ "to_list", func_to_list },
	{ NULL, NULL },
};
