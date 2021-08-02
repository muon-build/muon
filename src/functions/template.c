#include "posix.h"

#include "functions/common.h"
#include "functions/xxx.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
}

const struct func_impl_name impl_tbl_xxx[] = {
	{ "", func_ },
	{ NULL, NULL },
};
