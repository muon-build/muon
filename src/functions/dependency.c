#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/default/dependency.h"
#include "functions/dependency.h"
#include "interpreter.h"
#include "log.h"

static bool
func_dependency_found(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	struct obj *res = make_obj(wk, obj, obj_bool);
	res->dat.boolean = (get_obj(wk, rcvr)->dat.dep.flags & dep_flag_found)
			   == dep_flag_found;

	return true;
}

#define BUF_LEN 2048

static bool
func_dependency_get_pkgconfig_variable(struct workspace *wk, uint32_t rcvr,
	uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	if (!(get_obj(wk, rcvr)->dat.dep.flags & dep_flag_pkg_config)) {
		interp_error(wk, args_node, "this dependency is not from pkg_config");
		return false;
	}

	char arg[BUF_LEN] = "--variable=";
	const uint32_t len = strlen(arg);
	strncpy(&arg[len], wk_objstr(wk, an[0].val), BUF_LEN - len);

	struct run_cmd_ctx ctx = { 0 };
	if (!pkg_config(wk, &ctx, an[0].node, arg, wk_objstr(wk, get_obj(wk, rcvr)->dat.dep.name))) {
		return false;
	}

	bool empty = true;
	const char *s;
	for (s = ctx.out; *s; ++s) {
		if (!(*s == ' ' || *s == '\n')) {
			empty = false;
			break;
		}
	}

	if (empty) {
		interp_error(wk, an[0].node, "undefined pkg_config variable");
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push_stripped(wk, ctx.out);

	return true;
}

const struct func_impl_name impl_tbl_dependency[] = {
	{ "found", func_dependency_found },
	{ "get_pkgconfig_variable", func_dependency_get_pkgconfig_variable },
	{ NULL, NULL },
};
