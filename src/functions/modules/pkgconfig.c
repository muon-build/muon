#include "posix.h"

#include <assert.h>

#include "functions/modules/pkgconfig.h"
#include "lang/interpreter.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static bool
func_module_pkgconfig_generate(struct workspace *wk, obj rcvr, uint32_t args_node, obj *obj)
{
	struct args_norm ao[] = { { obj_build_target }, ARG_TYPE_NULL };
	enum kwargs {
		kw_name,
		kw_description,
		kw_extra_cflags,
		kw_filebase,
		kw_install_dir,
		kw_libraries,
		kw_libraries_private,
		kw_subdirs,
		kw_requires,
		kw_requires_private,
		kw_url,
		kw_variables,
		kw_unescaped_variables,
		kw_uninstalled_variables,
		kw_unescaped_uninstalled_variables,
		kw_version,
		kw_dataonly,
		kw_conflicts,
	};
	struct args_kw akw[] = {
		[kw_name] = { "name", obj_string },
		[kw_description] = { "description", obj_string },
		[kw_extra_cflags] = { "extra_cflags", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_filebase] = { "filebase", obj_string },
		[kw_install_dir] = { "install_dir", obj_string },
		[kw_libraries] = { "libraries", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_libraries_private] = { "libraries_private", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_subdirs] = { "subdirs", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_requires] = { "requires", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_requires_private] = { "requires_private", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_url] = { "url", obj_string },
		[kw_variables] = { "variables", obj_any },
		[kw_unescaped_variables] = { "unescaped_variables", obj_any },
		[kw_uninstalled_variables] = { "uninstalled_variables", obj_any },
		[kw_unescaped_uninstalled_variables] = { "unescaped_uninstalled_variables", obj_any },
		[kw_version] = { "version", obj_string },
		[kw_dataonly] = { "dataonly", obj_bool },
		[kw_conflicts] = { "conflicts", ARG_TYPE_ARRAY_OF | obj_string },
		0
	};
	if (!interp_args(wk, args_node, NULL, ao, akw)) {
		return false;
	}

	if (!ao[0].set && (!akw[kw_name].set || !akw[kw_description].set)) {
		interp_error(wk, args_node, "you must either pass a library, "
			"or the name and description keywords");
	}

	str name = 0, desc = 0;
	if (ao[0].set) {
		name = get_obj(wk, ao[0].val)->dat.tgt.name;
		if (!akw[kw_description].set) {
			desc = wk_str_pushf(wk, "generated pc file for %s", get_cstr(wk, name));
		}
	}

	if (akw[kw_name].set) {
		name = get_obj(wk, akw[kw_name].val)->dat.str;
	}

	if (akw[kw_description].set) {
		desc = get_obj(wk, akw[kw_description].val)->dat.str;
	}

	assert(name && desc);

	str filebase = name;
	if (akw[kw_filebase].set) {
		filebase = get_obj(wk, akw[kw_filebase].val)->dat.str;
	}

	char path[PATH_MAX];
	if (!path_join(path, PATH_MAX, wk->muon_private, get_cstr(wk, filebase))) {
		return false;
	} else if (!path_add_suffix(path, PATH_MAX, ".pc")) {
		return false;
	}

	FILE *f;
	if (!(f = fs_fopen(path, "wb"))) {
		return false;
	}

	fprintf(f,
		"prefix=/usr/local\n"
		"libdir=${prefix}/lib\n"
		"includedir=${prefix}/include\n"
		"\n"
		"Name: %s\n"
		"Description: %s\n"
		"Cflags: -I${includedir}\n",
		get_cstr(wk, name),
		get_cstr(wk, desc)
		);

	{
		const char *ver = "undefined";
		if (akw[kw_version].set) {
			ver = get_cstr(wk, akw[kw_version].val);
		}
		fprintf(f, "Version: %s\n", ver);
	}

	if (!fs_fclose(f)) {
		return false;
	}

	return true;
}
const struct func_impl_name impl_tbl_module_pkgconfig[] = {
	{ "generate", func_module_pkgconfig_generate },
	{ NULL, NULL },
};
