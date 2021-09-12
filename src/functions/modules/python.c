#include "posix.h"

#include "functions/modules/python.h"
#include "lang/interpreter.h"
#include "platform/filesystem.h"

static bool
func_module_python_find_python(struct workspace *wk, obj rcvr, uint32_t args_node, obj *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *cmd_path;
	if (!fs_find_cmd("python3", &cmd_path)) {
		interp_error(wk, args_node, "python3 not found");
		return false;
	}

	struct obj *external_program = make_obj(wk, obj, obj_external_program);
	external_program->dat.external_program.found = true;
	external_program->dat.external_program.full_path = wk_str_push(wk, cmd_path);
	return true;
}


const struct func_impl_name impl_tbl_module_python[] = {
	{ "find_installation", func_module_python_find_python },
	{ NULL, NULL },
};

const struct func_impl_name impl_tbl_module_python3[] = {
	{ "find_python", func_module_python_find_python },
	{ NULL, NULL },
};
