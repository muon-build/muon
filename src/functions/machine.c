#include "posix.h"

#include "functions/common.h"
#include "functions/machine.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_machine_system(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, "linux");

	return true;
}

static bool
func_machine_endian(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, "little");

	return true;
}

const struct func_impl_name impl_tbl_machine[] = {
	{ "system", func_machine_system },
	{ "endian", func_machine_endian },
	{ NULL, NULL },
};
