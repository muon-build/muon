#include "posix.h"

#include "functions/modules/python.h"
#include "lang/interpreter.h"
#include "platform/filesystem.h"

static bool
func_module_python_find_python(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, NULL, ao, NULL)) {
		return false;
	}

	const char *cmd = "python3";
	if (ao[0].set) {
		cmd = get_cstr(wk, ao[0].val);
	}

	const char *cmd_path;
	if (!fs_find_cmd(cmd, &cmd_path)) {
		interp_error(wk, args_node, "python3 not found");
		return false;
	}

	make_obj(wk, res, obj_external_program);
	struct obj_external_program *ep = get_obj_external_program(wk, *res);
	ep->found = true;
	ep->full_path = make_str(wk, cmd_path);
	return true;
}

const struct func_impl_name impl_tbl_module_python[] = {
	{ "find_installation", func_module_python_find_python, tc_external_program },
	{ NULL, NULL },
};

const struct func_impl_name impl_tbl_module_python3[] = {
	{ "find_python", func_module_python_find_python, tc_external_program },
	{ NULL, NULL },
};
