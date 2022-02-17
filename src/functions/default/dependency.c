#include "posix.h"

#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "external/libpkgconf.h"
#include "functions/common.h"
#include "functions/default/dependency.h"
#include "functions/default/subproject.h"
#include "functions/string.h"
#include "functions/subproject.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/run_cmd.h"

enum dep_not_found_reason {
	dep_not_found_reason_not_found,
	dep_not_found_reason_version,
};

struct dep_lookup_ctx {
	obj *res;
	struct args_kw *default_options, *versions;
	enum dep_not_found_reason not_found_reason;
	enum requirement_type requirement;
	uint32_t err_node;
	uint32_t fallback_node;
	obj name;
	obj fallback;
	obj not_found_message;
	bool is_static;
	bool disabler;
};

static bool
check_dependency_version(struct workspace *wk, obj dep_ver_str, uint32_t err_node, obj ver, bool *res)
{
	if (!ver) {
		*res = true;
		return true;
	}

	if (!version_compare(wk, err_node, get_str(wk, dep_ver_str), ver, res)) {
		return false;
	}

	return true;
}

static bool
handle_dependency_fallback(struct workspace *wk, struct dep_lookup_ctx *ctx, bool *found)
{
	ctx->not_found_reason = 0;

	obj subproj_name, subproj_dep = 0, subproj;

	switch (get_obj_array(wk, ctx->fallback)->len) {
	case 0:
		assert(false && "this should not be!");
		break;
	case 2:
		obj_array_index(wk, ctx->fallback, 1, &subproj_dep);
	/* FALLTHROUGH */
	case 1:
		obj_array_index(wk, ctx->fallback, 0, &subproj_name);
		break;
	default:
		interp_error(wk, ctx->err_node, "expected array of length 1-2 for fallback");
		return false;
	}


	if (!subproject(wk, subproj_name, ctx->requirement, ctx->default_options, ctx->versions, &subproj)) {
		return false;
	}

	if (!get_obj_subproject(wk, subproj)->found) {
		*found = false;
		return true;
	}

	if (subproj_dep) {
		if (!subproject_get_variable(wk, ctx->fallback_node, subproj_dep, 0, subproj, ctx->res)) {
			return false;
		}
	} else {
		if (!obj_dict_index(wk, wk->dep_overrides, ctx->name, ctx->res)) {
			interp_error(wk, ctx->fallback_node, "subproject does not override dependency %o", ctx->name);
			return false;
		}
	}

	if (!typecheck(wk, ctx->fallback_node, *ctx->res, obj_dependency)) {
		return false;
	}

	*found = true;
	return true;
}

static bool
get_dependency_pkgconfig(struct workspace *wk, struct dep_lookup_ctx *ctx, bool *found)
{
	struct pkgconf_info info = { 0 };
	*found = false;

	if (!muon_pkgconf_lookup(wk, ctx->name, ctx->is_static, &info)) {
		return true;
	}

	obj ver_str = make_str(wk, info.version);
	bool ver_match;
	if (!check_dependency_version(wk, ver_str, ctx->err_node, ctx->versions->val, &ver_match)) {
		return false;
	} else if (!ver_match) {
		ctx->not_found_reason = dep_not_found_reason_version;
		return true;
	}

	make_obj(wk, ctx->res, obj_dependency);
	struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);
	dep->name = ctx->name;
	dep->version = ver_str;
	dep->flags |= dep_flag_found | dep_flag_pkg_config;
	dep->link_with = info.libs;
	dep->link_with_not_found = info.not_found_libs;
	dep->include_directories = info.includes;
	dep->compile_args = info.compile_args;
	dep->link_args = info.link_args;

	*found = true;
	return true;
}

static bool
get_dependency(struct workspace *wk, struct dep_lookup_ctx *ctx)
{
	bool found = false;

	{
		obj dep;
		if (obj_dict_index(wk, wk->dep_overrides, ctx->name, &dep)) {
			*ctx->res = dep;
			found = true;
		}
	}

	if (!found && !get_dependency_pkgconfig(wk, ctx, &found)) {
		return false;
	}

	if (!found && ctx->fallback) {
		if (!handle_dependency_fallback(wk, ctx, &found)) {
			return false;
		}
	}

	if (!found) {
		LLOG_W("dependency %s not found", get_cstr(wk, ctx->name));

		if (ctx->not_found_reason == dep_not_found_reason_version) {
			struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);

			obj_fprintf(wk, log_file(), ": bad verson, have: %o, but need %o\n",
				dep->version, ctx->versions->val);
		}

		if (ctx->not_found_message) {
			obj_fprintf(wk, log_file(), ", %#o", ctx->not_found_message);
		}

		log_plain("\n");

		if (ctx->requirement == requirement_required) {
			interp_error(wk, ctx->err_node, "required dependency not found");
			return false;
		} else {
			if (ctx->disabler) {
				*ctx->res = disabler_id;
			} else {
				make_obj(wk, ctx->res, obj_dependency);
				get_obj_dependency(wk, *ctx->res)->name = ctx->name;
			}
		}
	} else {
		struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);

		LOG_I("found dependency %s%s%s%s",
			get_cstr(wk, dep->name),
			dep->version ? " version: " : "",
			dep->version ? get_cstr(wk, dep->version) : "",
			ctx->is_static ? ", static" : "");
	}

	return true;
}

static bool
handle_special_dependency(struct workspace *wk, struct dep_lookup_ctx *ctx, bool *handled)
{
	if (strcmp(get_cstr(wk, ctx->name), "threads") == 0) {
		LOG_I("dependency threads found");

		*handled = true;
		make_obj(wk, ctx->res, obj_dependency);
		struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);
		dep->name = ctx->name;
		dep->flags |= dep_flag_found;

		make_obj(wk, &dep->compile_args, obj_array);
		obj_array_push(wk, dep->compile_args, make_str(wk, "-pthread"));

		make_obj(wk, &dep->link_args, obj_array);
		obj_array_push(wk, dep->link_args, make_str(wk, "-pthread"));
	} else if (strcmp(get_cstr(wk, ctx->name), "curses") == 0) {
		*handled = true;
		ctx->name = make_str(wk, "ncurses");
		if (!get_dependency(wk, ctx)) {
			return false;
		}
	} else if (strcmp(get_cstr(wk, ctx->name), "") == 0) {
		*handled = true;
		if (ctx->requirement == requirement_required) {
			interp_error(wk, ctx->err_node, "dependency '' cannot be required");
			return false;
		}
		make_obj(wk, ctx->res, obj_dependency);
	} else {
		*handled = false;
	}

	return true;
}

bool
func_dependency(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_native, // ignored
		kw_version,
		kw_static,
		kw_modules, // ignored
		kw_fallback,
		kw_allow_fallback,
		kw_default_options,
		kw_not_found_message,
		kw_disabler,
		kw_method,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required" },
		[kw_native] = { "native", obj_bool },
		[kw_version] = { "version", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_static] = { "static", obj_bool },
		[kw_modules] = { "modules", obj_array },
		[kw_fallback] = { "fallback", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_allow_fallback] = { "allow_fallback", obj_bool },
		[kw_default_options] = { "default_options", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_not_found_message] = { "not_found_message", obj_string },
		[kw_disabler] = { "disabler", obj_bool },
		[kw_method] = { "method", obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (akw[kw_method].set) {
		if (!(str_eql(get_str(wk, akw[kw_method].val), &WKSTR("pkg-config"))
		      || str_eql(get_str(wk, akw[kw_method].val), &WKSTR("auto")))) {
			interp_error(wk, akw[kw_method].node, "unsupported dependency method %o", akw[kw_method].val);
			return false;
		}
	}

	enum requirement_type requirement;
	if (!coerce_requirement(wk, &akw[kw_required], &requirement)) {
		return false;
	}

	if (requirement == requirement_skip) {
		make_obj(wk, res, obj_dependency);
		struct obj_dependency *dep = get_obj_dependency(wk, *res);
		dep->name = an[0].val;
		return true;
	}

	bool is_static = false;
	if (akw[kw_static].set && get_obj_bool(wk, akw[kw_static].val)) {
		is_static = true;
	}

	/* A fallback is allowed if
	 * - allow_fallback: true
	 * - allow_fallback is not specified but the requirement is required
	 * - allow_fallback is not specified, but the fallback keyword is
	 *   specified with at least one value (i.e. not an empty array)
	 */
	bool fallback_allowed =
		(akw[kw_allow_fallback].set && get_obj_bool(wk, akw[kw_allow_fallback].val))
		|| (!akw[kw_allow_fallback].set &&
		    ((requirement == requirement_required)
		     || (akw[kw_fallback].set && get_obj_array(wk, akw[kw_fallback].val)->len)));

	uint32_t fallback_err_node = 0;
	obj fallback = 0;
	if (fallback_allowed) {
		if (akw[kw_fallback].set) {
			fallback_err_node = akw[kw_fallback].node;
			fallback = akw[kw_fallback].val;
		} else if (akw[kw_allow_fallback].set) {
			fallback_err_node = akw[kw_allow_fallback].node;
		} else {
			fallback_err_node = an[0].node;
		}

		if (!fallback) {
			make_obj(wk, &fallback, obj_array);
			obj_array_push(wk, fallback, an[0].val);
		}
	}

	struct dep_lookup_ctx ctx = {
		.res = res,
		.requirement = requirement,
		.versions = &akw[kw_version],
		.err_node = an[0].node,
		.fallback_node = fallback_err_node,
		.name = an[0].val,
		.fallback = fallback,
		.default_options = &akw[kw_default_options],
		.not_found_message = akw[kw_not_found_message].val,
		.is_static = is_static,
		.disabler = akw[kw_disabler].set && get_obj_bool(wk, akw[kw_disabler].val),
	};

	bool handled;
	if (!handle_special_dependency(wk, &ctx, &handled)) {
		return false;
	} else if (!handled) {
		if (!get_dependency(wk, &ctx)) {
			return false;
		}
	}

	return true;
}

bool
func_declare_dependency(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	enum kwargs {
		kw_sources,
		kw_link_with,
		kw_link_whole, // TODO
		kw_link_args,
		kw_dependencies, // TODO
		kw_version,
		kw_include_directories,
		kw_variables,
		kw_compile_args,
	};
	struct args_kw akw[] = {
		[kw_sources] = { "sources", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_link_with] = { "link_with", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_link_whole] = { "link_whole", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_link_args] = { "link_args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_version] = { "version", obj_string },
		[kw_include_directories] = { "include_directories", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_variables] = { "variables", obj_dict },
		[kw_compile_args] = { "compile_args", ARG_TYPE_ARRAY_OF | obj_string },
		0
	};

	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	if (akw[kw_include_directories].set) {
		obj inc_dirs;
		if (!coerce_include_dirs(wk, akw[kw_include_directories].node, akw[kw_include_directories].val, false, &inc_dirs)) {
			return false;
		}

		akw[kw_include_directories].val = inc_dirs;
	}

	make_obj(wk, res, obj_dependency);
	struct obj_dependency *dep = get_obj_dependency(wk, *res);

	dep->name = make_strf(wk, "%s:declared_dep@%s:%d",
		get_cstr(wk, current_project(wk)->cfg.name),
		wk->src->label,
		get_node(wk->ast, args_node)->line);

	dep->link_args = akw[kw_link_args].val;
	dep->version = akw[kw_version].val;
	dep->flags |= dep_flag_found;
	dep->variables = akw[kw_variables].val;
	dep->deps = akw[kw_dependencies].val;
	dep->compile_args = akw[kw_compile_args].val;

	if (akw[kw_sources].set) {
		obj sources;
		if (!coerce_files(wk, akw[kw_sources].node, akw[kw_sources].val, &sources)) {
			return false;
		}

		dep->sources = sources;
	}

	make_obj(wk, &dep->link_with, obj_array);
	if (akw[kw_link_with].set) {
		obj arr;
		obj_array_dup(wk, akw[kw_link_with].val, &arr);
		obj_array_extend(wk, dep->link_with, arr);
	}

	if (akw[kw_link_whole].set) {
		obj arr;
		obj_array_dup(wk, akw[kw_link_whole].val, &arr);
		obj_array_extend(wk, dep->link_with, arr);
	}

	if (akw[kw_include_directories].set) {
		dep->include_directories = akw[kw_include_directories].val;
	}

	return true;
}
