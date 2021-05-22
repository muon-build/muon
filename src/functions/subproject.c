#include "posix.h"

#include "functions/common.h"
#include "functions/subproject.h"
#include "interpreter.h"
#include "log.h"

static bool
func_subproject_get_variable(struct ast *ast, struct workspace *wk, uint32_t rcvr, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	const char *name = wk_objstr(wk, an[0].val);
	uint32_t subproj = get_obj(wk, rcvr)->dat.subproj;

	return get_obj_id(wk, name, obj, subproj);
}

const struct func_impl_name impl_tbl_subproject[] = {
	{ "get_variable", func_subproject_get_variable },
	{ NULL, NULL },
};
