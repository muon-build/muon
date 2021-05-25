#include "posix.h"

#include "functions/common.h"
#include "functions/dependency.h"
#include "interpreter.h"
#include "log.h"

static bool
func_dependency_found(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	// TODO
	struct obj *res = make_obj(wk, obj, obj_bool);
	res->dat.boolean = false;

	return true;
}

const struct func_impl_name impl_tbl_dependency[] = {
	{ "found", func_dependency_found },
	{ NULL, NULL },
};
