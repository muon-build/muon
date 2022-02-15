#include "posix.h"

#include "functions/common.h"
#include "functions/run_result.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_run_result_returncode(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_number);
	set_obj_number(wk, *res, get_obj_run_result(wk, rcvr)->status);
	return true;
}

static bool
func_run_result_stdout(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_run_result(wk, rcvr)->out;
	return true;
}

static bool
func_run_result_stderr(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_run_result(wk, rcvr)->err;
	return true;
}

const struct func_impl_name impl_tbl_run_result[] = {
	{ "returncode", func_run_result_returncode },
	{ "stderr", func_run_result_stderr },
	{ "stdout", func_run_result_stdout },
	{ NULL, NULL },
};
