#include "posix.h"

#include "functions/common.h"
#include "functions/subproject.h"
#include "lang/interpreter.h"
#include "log.h"

bool
subproject_get_variable(struct workspace *wk, uint32_t node, uint32_t name_id,
	uint32_t subproj, uint32_t *obj)
{
	const char *name = get_cstr(wk, name_id);
	uint32_t subproject_id = get_obj(wk, subproj)->dat.subproj;

	if (!get_obj_id(wk, name, obj, subproject_id)) {
		interp_error(wk, node, "subproject does not define '%s'", name);
		return false;
	}

	return true;
}

static bool
func_subproject_get_variable(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	return subproject_get_variable(wk, an[0].node, an[0].val, rcvr, obj);
}

const struct func_impl_name impl_tbl_subproject[] = {
	{ "get_variable", func_subproject_get_variable },
	{ NULL, NULL },
};
