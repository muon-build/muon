#include "posix.h"

#include "functions/common.h"
#include "functions/subproject.h"
#include "interpreter.h"
#include "log.h"

static bool
func_subproject_get_variable(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	const char *name = wk_objstr(wk, an[0].val);
	uint32_t subproj = get_obj(wk, rcvr)->dat.subproj;

	if (!get_obj_id(wk, name, obj, subproj)) {
		interp_error(wk, an[0].node, "subproject does not define '%s'", name);
		return false;
	}

	return true;
}

const struct func_impl_name impl_tbl_subproject[] = {
	{ "get_variable", func_subproject_get_variable },
	{ NULL, NULL },
};
