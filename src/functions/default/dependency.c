#include "posix.h"

#include <string.h>

#include "coerce.h"
#include "external/pkgconf.h"
#include "functions/common.h"
#include "functions/default/dependency.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/run_cmd.h"

static bool
get_dependency(struct workspace *wk, uint32_t *obj, uint32_t node, uint32_t name, bool is_static, enum requirement_type requirement)
{
	struct pkgconf_info info = { 0 };

	if (!muon_pkgconf_lookup(wk, get_obj(wk, name)->dat.str, is_static, &info)) {
		if (requirement == requirement_required) {
			interp_error(wk, node, "required dependency not found");
			return false;
		}

		struct obj *dep = make_obj(wk, obj, obj_dependency);
		dep->dat.dep.name = name;
		LOG_I("dependency %s not found", wk_objstr(wk, name));
		return true;
	}

	struct obj *dep = make_obj(wk, obj, obj_dependency);
	dep->dat.dep.name = name;
	dep->dat.dep.version = wk_str_push(wk, info.version);
	dep->dat.dep.flags |= dep_flag_found | dep_flag_pkg_config;
	dep->dat.dep.link_with = info.libs;
	dep->dat.dep.include_directories = info.includes;

	LOG_I("dependency %s found: %s, %s", wk_objstr(wk, name), wk_str(wk, dep->dat.dep.version),
		is_static ? "static" : "dynamic");

	return true;
}

static bool
handle_special_dependency(struct workspace *wk, uint32_t node, uint32_t name,
	bool is_static, enum requirement_type requirement, uint32_t *obj, bool *handled)
{
	if (strcmp(wk_objstr(wk, name), "threads") == 0) {
		*handled = true;
		struct obj *dep = make_obj(wk, obj, obj_dependency);
		dep->dat.dep.name = get_obj(wk, name)->dat.str;
		dep->dat.dep.flags |= dep_flag_found;
	} else if (strcmp(wk_objstr(wk, name), "curses") == 0) {
		*handled = true;
		uint32_t s;
		make_obj(wk, &s, obj_string)->dat.str = wk_str_push(wk, "ncurses");
		if (!get_dependency(wk, obj, node, s, is_static, requirement)) {
			return false;
		}
	} else {
		*handled = false;
	}

	return true;
}

struct parse_cflags_iter_ctx {
	uint32_t include_directories;
};

bool
func_dependency(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_native,
		kw_version,
		kw_static,
		kw_modules,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required" },
		[kw_native] = { "native", obj_bool },
		[kw_version] = { "version", obj_string },
		[kw_static] = { "static", obj_bool },
		[kw_modules] = { "modules", obj_array },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum requirement_type requirement;
	if (!coerce_requirement(wk, &akw[kw_required], &requirement)) {
		return false;
	}

	if (requirement == requirement_skip) {
		struct obj *dep = make_obj(wk, obj, obj_dependency);
		dep->dat.dep.name = an[0].val;
		return true;
	}

	bool is_static = false;
	if (akw[kw_static].set && get_obj(wk, akw[kw_static].val)->dat.boolean) {
		is_static = true;
	}

	bool handled;
	if (!handle_special_dependency(wk, an[0].node, an[0].val, is_static, requirement, obj, &handled)) {
		return false;
	} else if (handled) {
		return true;
	}

	return get_dependency(wk, obj, an[0].node, an[0].val, is_static, requirement);
}
