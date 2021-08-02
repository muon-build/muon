#include "posix.h"

#include <string.h>

#include "external/pkgconf.h"
#include "functions/common.h"
#include "functions/dependency.h"
#include "lang/interpreter.h"
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

	uint32_t res;
	if (!muon_pkgconf_get_variable(wk, wk_objstr(wk, get_obj(wk, rcvr)->dat.dep.name), wk_objstr(wk, an[0].val), &res)) {
		interp_error(wk, an[0].node, "undefined pkg_config variable");
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = res;
	return true;
}

static bool
func_dependency_version(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	uint32_t version = get_obj(wk, rcvr)->dat.dep.version;

	if (version) {
		make_obj(wk, obj, obj_string)->dat.str = version;
		return true;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, "unknown");
	return true;
}

const struct func_impl_name impl_tbl_dependency[] = {
	{ "found", func_dependency_found },
	{ "get_pkgconfig_variable", func_dependency_get_pkgconfig_variable },
	{ "version", func_dependency_version },
	{ NULL, NULL },
};
