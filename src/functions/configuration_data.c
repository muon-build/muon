#include "posix.h"

#include "functions/common.h"
#include "functions/configuration_data.h"
#include "interpreter.h"
#include "log.h"

static bool
func_configuration_data_set(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{

	return true;
}

const struct func_impl_name impl_tbl_configuration_data[] = {
	{ "set", func_configuration_data_set },
	{ NULL, NULL },
};
