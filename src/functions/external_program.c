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

	make_obj(wk, obj, obj_bool);
	set_obj_bool(wk, *obj, get_obj_external_program(wk, rcvr)->found);
	return true;
}

static bool
func_external_program_path(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = get_obj_external_program(wk, rcvr)->full_path;
	return true;
}

const struct func_impl_name impl_tbl_external_program[] = {
	{ "found", func_external_program_found },
	{ "path", func_external_program_path },
	{ "full_path", func_external_program_path },
	{ NULL, NULL },
};
