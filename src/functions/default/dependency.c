#include "posix.h"

#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "external/pkgconf.h"
#include "functions/common.h"
#include "functions/default/dependency.h"
#include "functions/subproject.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/run_cmd.h"

static bool
handle_dependency_fallback(struct workspace *wk, uint32_t *obj, uint32_t node, uint32_t name,
	uint32_t fallback)
{
	if (get_obj(wk, fallback)->dat.arr.len != 2) {
		interp_error(wk, node, "expected array of length 2 for fallback");
		return false;
	}

	uint32_t subproj_name, subproj_dep, subproj;
	obj_array_index(wk, fallback, 0, &subproj_name);
	obj_array_index(wk, fallback, 1, &subproj_dep);

	char src[BUF_SIZE_2k];
	snprintf(src, BUF_SIZE_2k, "subproject('%s')", wk_objstr(wk, subproj_name));
	if (!eval_str(wk, src, &subproj)) {
		return false;
	}

	return subproject_get_variable(wk, node, subproj_dep, subproj, obj);
}

static bool
get_dependency(struct workspace *wk, uint32_t *obj, uint32_t node, uint32_t name,
	bool is_static, uint32_t fallback, enum requirement_type requirement)
{
	struct pkgconf_info info = { 0 };

	if (!muon_pkgconf_lookup(wk, get_obj(wk, name)->dat.str, is_static, &info)) {
		if (fallback) {
			return handle_dependency_fallback(wk, obj, node, name, fallback);
		}

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
	dep->dat.dep.version = make_str(wk, info.version);
	dep->dat.dep.flags |= dep_flag_found | dep_flag_pkg_config;
	dep->dat.dep.link_with = info.libs;
	dep->dat.dep.include_directories = info.includes;

	LOG_I("dependency %s found: %s%s", wk_objstr(wk, name), wk_objstr(wk, dep->dat.dep.version),
		is_static ? ", static" : "");

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
		if (!get_dependency(wk, obj, node, s, is_static, 0, requirement)) {
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
		kw_native, // ignored
		kw_version, // ignored
		kw_static,
		kw_modules, // ignored
		kw_fallback, // ignored
	};
	struct args_kw akw[] = {
		[kw_required] = { "required" },
		[kw_native] = { "native", obj_bool },
		[kw_version] = { "version", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_static] = { "static", obj_bool },
		[kw_modules] = { "modules", obj_array },
		[kw_fallback] = { "fallback", ARG_TYPE_ARRAY_OF | obj_string },
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

	return get_dependency(wk, obj, an[0].node, an[0].val, is_static,
		akw[kw_fallback].val, requirement);
}

bool
func_declare_dependency(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	enum kwargs {
		kw_link_with,
		kw_link_whole, // TODO
		kw_link_args,
		kw_dependencies, // TODO
		kw_version,
		kw_include_directories,
	};
	struct args_kw akw[] = {
		[kw_link_with] = { "link_with", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_link_whole] = { "link_whole", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_link_args] = { "link_args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_version] = { "version", obj_string },
		[kw_include_directories] = { "include_directories", ARG_TYPE_ARRAY_OF | obj_any },
		0
	};

	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	if (akw[kw_include_directories].set) {
		uint32_t inc_dirs;

		if (!coerce_dirs(wk, akw[kw_include_directories].node, akw[kw_include_directories].val, &inc_dirs)) {
			return false;
		}

		akw[kw_include_directories].val = inc_dirs;
	}

	struct obj *dep = make_obj(wk, obj, obj_dependency);
	dep->dat.dep.name = wk_str_pushf(wk, "%s:declared_dep", wk_str(wk, current_project(wk)->cfg.name));
	dep->dat.dep.link_args = akw[kw_link_args].val;
	dep->dat.dep.version = akw[kw_version].val;
	dep->dat.dep.flags |= dep_flag_found;

	if (akw[kw_link_with].set) {
		dep->dat.dep.link_with = akw[kw_link_with].val;
	}

	if (akw[kw_include_directories].set) {
		dep->dat.dep.include_directories = akw[kw_include_directories].val;
	}

	return true;
}

