#include "posix.h"

#include "functions/build_target.h"
#include "functions/common.h"
#include "interpreter.h"
#include "log.h"

static bool
func_build_target_name(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = get_obj(wk, rcvr)->dat.tgt.name;
	return true;
}

const struct func_impl_name impl_tbl_build_target[] = {
	{ "name", func_build_target_name },
	{ NULL, NULL },
};
