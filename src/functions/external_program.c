#include "posix.h"

#include "functions/common.h"
#include "functions/external_program.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_external_program_found(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	struct obj *res = make_obj(wk, obj, obj_bool);
	res->dat.boolean = get_obj(wk, rcvr)->dat.external_program.found;

	return true;
}

static bool
func_external_program_path(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	struct obj *res = make_obj(wk, obj, obj_string);
	res->dat.str = get_obj(wk, rcvr)->dat.external_program.full_path;

	return true;
}

const struct func_impl_name impl_tbl_external_program[] = {
	{ "found", func_external_program_found },
	{ "path", func_external_program_path },
	{ "full_path", func_external_program_path },
	{ NULL, NULL },
};
