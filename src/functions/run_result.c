#include "posix.h"

#include "functions/common.h"
#include "functions/run_result.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_run_result_returncode(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_number);
	set_obj_number(wk, *obj, get_obj_run_result(wk, rcvr)->status);
	return true;
}

static bool
func_run_result_stdout(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = get_obj_run_result(wk, rcvr)->out;
	return true;
}

static bool
func_run_result_stderr(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*obj = get_obj_run_result(wk, rcvr)->err;
	return true;
}

const struct func_impl_name impl_tbl_run_result[] = {
	{ "returncode", func_run_result_returncode },
	{ "stderr", func_run_result_stderr },
	{ "stdout", func_run_result_stdout },
	{ NULL, NULL },
};
