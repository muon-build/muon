#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/modules.h"
#include "functions/modules/fs.h"
#include "functions/modules/pkgconfig.h"
#include "functions/modules/python.h"
#include "log.h"

const char *module_names[module_count] = {
	[module_fs] = "fs",
	[module_python] = "python",
	[module_python3] = "python3",
	[module_pkgconfig] = "pkgconfig",
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

static bool
func_module_found(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res, get_obj_module(wk, rcvr)->found);
	return true;
}

static const struct func_impl_name *module_func_tbl[module_count] = {
	[module_fs] = impl_tbl_module_fs,
	[module_python] = impl_tbl_module_python,
	[module_python3] = impl_tbl_module_python3,
	[module_pkgconfig] = impl_tbl_module_pkgconfig,
};

const struct func_impl_name *
module_func_lookup(const char *name, enum module mod)
{
	static const struct func_impl_name func_module_found_impl = {
		.name = "found",
		.func = func_module_found,
		.return_type = tc_bool,
		.pure = true,
	};

	if (strcmp(name, "found") == 0) {
		return &func_module_found_impl;
	}

	const struct func_impl_name *fi;
	if (!(fi = func_lookup(module_func_tbl[mod], name))) {
		return false;
	}

	return fi;
}
