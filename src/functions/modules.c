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

	// unimplemented
	[module_cmake] = "cmake",
	[module_dlang] = "dlang",
	[module_gnome] = "gnome",
	[module_hotdoc] = "hotdoc",
	[module_i18n] = "i18n",
	[module_java] = "java",
	[module_keyval] = "keyval",
	[module_modtest] = "modtest",
	[module_qt] = "qt",
	[module_qt4] = "qt4",
	[module_qt5] = "qt5",
	[module_qt6] = "qt6",
	[module_sourceset] = "sourceset",
	[module_unstable_cuda] = "unstable-cuda",
	[module_unstable_external_project] = "unstable-external_project",
	[module_unstable_icestorm] = "unstable-icestorm",
	[module_unstable_rust] = "unstable-rust",
	[module_unstable_simd] = "unstable-simd",
	[module_unstable_wayland] = "unstable-wayland",
	[module_windows] = "windows",
};

bool
module_lookup(const char *name, enum module *res, bool *has_impl)
{
	enum module i;
	for (i = 0; i < module_count; ++i) {
		if (strcmp(name, module_names[i]) == 0) {
			*res = i;
			*has_impl = i < module_unimplemented_separator;
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

static const struct func_impl_name *module_func_tbl[module_count][language_mode_count] = {
	[module_fs] = { impl_tbl_module_fs, impl_tbl_module_fs_internal },
	[module_python] = { impl_tbl_module_python },
	[module_python3] = { impl_tbl_module_python3 },
	[module_pkgconfig] = { impl_tbl_module_pkgconfig },
};

const struct func_impl_name *
module_func_lookup(struct workspace *wk, const char *name, enum module mod)
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

	if (!module_func_tbl[mod][0]) {
		return NULL;
	}

	const struct func_impl_name *fi;
	if (!(fi = func_lookup(module_func_tbl[mod][wk->lang_mode], name))) {
		return NULL;
	}

	return fi;
}
