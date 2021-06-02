#include "posix.h"

#include "functions/common.h"
#include "functions/meson.h"
#include "interpreter.h"
#include "log.h"

static bool
func_meson_get_compiler(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs { kw_native, };
	struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (!check_lang(wk, an[0].node, an[0].val)) {
		return false;
	}

	make_obj(wk, obj, obj_compiler);

	return true;
}

static bool
func_meson_project_version(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = current_project(wk)->cfg.version;
	return true;
}

const struct func_impl_name impl_tbl_meson[] = {
	{ "get_compiler", func_meson_get_compiler },
	{ "project_version", func_meson_project_version },
	{ NULL, NULL },
};
