#include "posix.h"

#include "functions/common.h"
#include "functions/subproject.h"
#include "lang/interpreter.h"
#include "log.h"

bool
subproject_get_variable(struct workspace *wk, uint32_t node, obj name_id,
	obj fallback, obj subproj, obj *res)
{
	const char *name = get_cstr(wk, name_id);
	struct obj *sub = get_obj(wk, subproj);
	assert(sub->type == obj_subproject);

	if (!sub->dat.subproj.found) {
		interp_error(wk, node, "subproject was not found");
		return false;
	}

	if (!get_obj_id(wk, name, res, sub->dat.subproj.id)) {
		if (!fallback) {
			interp_error(wk, node, "subproject does not define '%s'", name);
			return false;
		} else {
			*res = fallback;
		}
	}

	return true;
}

static bool
func_subproject_get_variable(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { obj_any }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, ao, NULL)) {
		return false;
	}

	return subproject_get_variable(wk, an[0].node, an[0].val, ao[0].val, rcvr, res);
}

static bool
func_subproject_found(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool)->dat.boolean = get_obj(wk, rcvr)->dat.subproj.found;
	return true;
}

const struct func_impl_name impl_tbl_subproject[] = {
	{ "found", func_subproject_found },
	{ "get_variable", func_subproject_get_variable },
	{ NULL, NULL },
};
