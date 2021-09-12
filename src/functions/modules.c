#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/modules.h"
#include "functions/modules/fs.h"
#include "functions/modules/python.h"
#include "log.h"

const char *module_names[module_count] = {
	[module_fs] = "fs",
	[module_python] = "python",
	[module_python3] = "python3",
};

bool
module_lookup(const char *name, enum module *res)
{
	enum module i;
	for (i = 0; i < module_count; ++i) {
		if (strcmp(name, module_names[i]) == 0) {
			*res = i;
			return true;
		}
	}

	return false;
}

static const struct func_impl_name *module_func_tbl[module_count] = {
	[module_fs] = impl_tbl_module_fs,
	[module_python] = impl_tbl_module_python,
	[module_python3] = impl_tbl_module_python3,
};

bool
module_lookup_func(const char *name, enum module mod, func_impl *res)
{
	return func_lookup(module_func_tbl[mod], name, res);
}
