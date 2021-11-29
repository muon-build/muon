#include "posix.h"

#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "external/libpkgconf.h"
#include "functions/common.h"
#include "functions/default/dependency.h"
#include "functions/string.h"
#include "functions/subproject.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/run_cmd.h"

enum dep_not_found_reason {
	dep_not_found_reason_not_found,
	dep_not_found_reason_version,
};

struct dep_lookup_ctx {
	obj *res;
	enum dep_not_found_reason not_found_reason;
	enum requirement_type requirement;
	uint32_t err_node;
	uint32_t fallback_node;
	obj name;
	obj fallback;
	obj default_options;
	obj not_found_message;
	obj version;
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

	struct version dep_ver;
	if (!string_to_version(wk, &dep_ver, get_str(wk, dep_ver_str))) {
		interp_error(wk, err_node,
			"dependency version %o not parseable",
			dep_ver_str);
		return false;
	}

	if (!version_compare(wk, err_node, &dep_ver, ver, res)) {
		return false;
	}

	return true;
}

static bool
handle_dependency_fallback(struct workspace *wk, struct dep_lookup_ctx *ctx, bool *found)
{
	ctx->not_found_reason = 0;

	if (get_obj(wk, ctx->fallback)->dat.arr.len != 2) {
		interp_error(wk, ctx->err_node, "expected array of length 2 for fallback");
		return false;
	}

	uint32_t subproj_name, subproj_dep, subproj;
	obj_array_index(wk, ctx->fallback, 0, &subproj_name);
	obj_array_index(wk, ctx->fallback, 1, &subproj_dep);

	char src[BUF_SIZE_2k];

	// TODO: make a proper eval_subproject() function to avoid this hacky
	// string evaluation
	const char *req = ctx->requirement == requirement_required ? "true" : "false";

	if (ctx->default_options) {
		obj_snprintf(wk, src, BUF_SIZE_2k, "subproject('%s', default_options: %o, required: %s)",
			get_cstr(wk, subproj_name), ctx->default_options, req);
	} else {
		obj_snprintf(wk, src, BUF_SIZE_2k, "subproject('%s', required: %s)", get_cstr(wk, subproj_name), req);
	}

	if (!eval_str(wk, src, &subproj)) {
		return false;
	}

	if (!get_obj(wk, subproj)->dat.subproj.found) {
		*found = false;
		return true;
	}

	if (!subproject_get_variable(wk, ctx->err_node, subproj_dep, subproj, ctx->res)) {
		return false;
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
	if (!check_dependency_version(wk, ver_str, ctx->err_node, ctx->version, &ver_match)) {
		return false;
	} else if (!ver_match) {
		ctx->not_found_reason = dep_not_found_reason_version;
		return true;
	}

	struct obj *dep = make_obj(wk, ctx->res, obj_dependency);
	dep->dat.dep.name = ctx->name;
	dep->dat.dep.version = ver_str;
	dep->dat.dep.flags |= dep_flag_found | dep_flag_pkg_config;
	dep->dat.dep.link_with = info.libs;
	dep->dat.dep.include_directories = info.includes;

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
			struct obj *dep = get_obj(wk, *ctx->res);

			obj_fprintf(wk, log_file(), ": bad verson, have: %o, but need %o\n",
				dep->dat.dep.version, ctx->version);
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
				struct obj *dep = make_obj(wk, ctx->res, obj_dependency);
				dep->dat.dep.name = ctx->name;
			}
		}
	} else {
		struct obj *dep = get_obj(wk, *ctx->res);

		assert(get_obj(wk, *ctx->res)->type == obj_dependency);

		LOG_I("dependency %s found: %s%s", get_cstr(wk, dep->dat.dep.name),
			get_cstr(wk, dep->dat.dep.version),
			ctx->is_static ? ", static" : "");
	}

	return true;
}

static bool
handle_special_dependency(struct workspace *wk, struct dep_lookup_ctx *ctx, bool *handled)
{
	if (strcmp(get_cstr(wk, ctx->name), "threads") == 0) {
		*handled = true;
		struct obj *dep = make_obj(wk, ctx->res, obj_dependency);
		dep->dat.dep.name = ctx->name;
		dep->dat.dep.flags |= dep_flag_found;
	} else if (strcmp(get_cstr(wk, ctx->name), "curses") == 0) {
		*handled = true;
		ctx->name = make_str(wk, "ncurses");
		if (!get_dependency(wk, ctx)) {
			return false;
		}
	} else if (strcmp(get_cstr(wk, ctx->name), "") == 0) {
		*handled = true;
		if (ctx->requirement == requirement_required) {
			interp_error(wk, ctx->err_node, "dependency '' is cannot be required");
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
		struct obj *dep = make_obj(wk, res, obj_dependency);
		dep->dat.dep.name = an[0].val;
		return true;
	}

	bool is_static = false;
	if (akw[kw_static].set && get_obj(wk, akw[kw_static].val)->dat.boolean) {
		is_static = true;
	}

	struct dep_lookup_ctx ctx = {
		.res = res,
		.requirement = requirement,
		.version = akw[kw_version].val,
		.err_node = an[0].node,
		.fallback_node = akw[kw_fallback].node,
		.name = an[0].val,
		.fallback = akw[kw_fallback].val,
		.default_options = akw[kw_default_options].val,
		.not_found_message = akw[kw_not_found_message].val,
		.is_static = is_static,
		.disabler = akw[kw_disabler].set
			? get_obj(wk, akw[kw_disabler].val)->dat.boolean
			: false,
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
func_declare_dependency(struct workspace *wk, uint32_t _, uint32_t args_node, obj *res)
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

	struct obj *dep = make_obj(wk, res, obj_dependency);
	dep->dat.dep.name = make_strf(wk, "%s:declared_dep", get_cstr(wk, current_project(wk)->cfg.name));
	dep->dat.dep.link_args = akw[kw_link_args].val;
	dep->dat.dep.version = akw[kw_version].val;
	dep->dat.dep.flags |= dep_flag_found;
	dep->dat.dep.variables = akw[kw_variables].val;

	if (akw[kw_sources].set) {
		obj sources;
		if (!coerce_files(wk, akw[kw_sources].node, akw[kw_sources].val, &sources)) {
			return false;
		}

		dep->dat.dep.sources = sources;
	}

	make_obj(wk, &dep->dat.dep.link_with, obj_array);
	if (akw[kw_link_with].set) {
		obj arr;
		obj_array_dup(wk, akw[kw_link_with].val, &arr);
		obj_array_extend(wk, dep->dat.dep.link_with, arr);
	}

	if (akw[kw_link_whole].set) {
		obj arr;
		obj_array_dup(wk, akw[kw_link_whole].val, &arr);
		obj_array_extend(wk, dep->dat.dep.link_with, arr);
	}

	if (akw[kw_include_directories].set) {
		dep->dat.dep.include_directories = akw[kw_include_directories].val;
	}

	return true;
}
