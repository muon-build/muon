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
#include "options.h"
#include "platform/filesystem.h"
#include "platform/run_cmd.h"

enum dep_lib_mode {
	dep_lib_mode_default,
	dep_lib_mode_static,
	dep_lib_mode_shared,
};

struct dep_lookup_ctx {
	obj *res;
	struct args_kw *default_options, *versions;
	enum requirement_type requirement;
	uint32_t err_node;
	uint32_t fallback_node;
	obj name;
	obj fallback;
	obj not_found_message;
	enum dep_lib_mode lib_mode;
	bool disabler;
	bool fallback_allowed;
	bool from_cache;
};

static bool
check_dependency_override(struct workspace *wk, struct dep_lookup_ctx *ctx, obj *res)
{
	if (ctx->lib_mode != dep_lib_mode_shared) {
		if (obj_dict_index(wk, wk->dep_overrides_static, ctx->name, res)) {
			ctx->lib_mode = dep_lib_mode_static;
			return true;
		}
	}

	if (ctx->lib_mode != dep_lib_mode_static) {
		if (obj_dict_index(wk, wk->dep_overrides_dynamic, ctx->name, res)) {
			ctx->lib_mode = dep_lib_mode_shared;
			return true;
		}
	}

	return false;
}

static bool
check_dependency_cache(struct workspace *wk, struct dep_lookup_ctx *ctx, obj *res)
{
	if (ctx->lib_mode != dep_lib_mode_shared) {
		if (obj_dict_index(wk, current_project(wk)->dep_cache.static_deps, ctx->name, res)) {
			ctx->lib_mode = dep_lib_mode_static;
			return true;
		}
	}

	if (ctx->lib_mode != dep_lib_mode_static) {
		if (obj_dict_index(wk, current_project(wk)->dep_cache.shared_deps, ctx->name, res)) {
			ctx->lib_mode = dep_lib_mode_shared;
			return true;
		}
	}

	return false;
}

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
	obj subproj_name, subproj_dep = 0, subproj;

	switch (get_obj_array(wk, ctx->fallback)->len) {
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

	if (ctx->lib_mode != dep_lib_mode_default) {
		obj libopt;
		if (ctx->lib_mode == dep_lib_mode_static) {
			libopt = make_str(wk, "default_library=static");
		} else {
			libopt = make_str(wk, "default_library=shared");
		}

		if (ctx->default_options->set) {
			if (!obj_array_in(wk, ctx->default_options->val, libopt)) {
				obj newopts;
				obj_array_dup(wk, ctx->default_options->val, &newopts);
				obj_array_push(wk, newopts, libopt);

				ctx->default_options->val = newopts;
			}
		} else {
			make_obj(wk, &ctx->default_options->val, obj_array);
			obj_array_push(wk, ctx->default_options->val, libopt);
			ctx->default_options->set = true;
		}
	}

	if (!subproject(wk, subproj_name, ctx->requirement, ctx->default_options, ctx->versions, &subproj)) {
		goto not_found;
	}

	if (!get_obj_subproject(wk, subproj)->found) {
		goto not_found;
	}

	if (subproj_dep) {
		if (!subproject_get_variable(wk, ctx->fallback_node, subproj_dep, 0, subproj, ctx->res)) {
			goto not_found;
		}
	} else {
		if (!check_dependency_override(wk, ctx, ctx->res)) {
			interp_warning(wk, ctx->fallback_node, "subproject does not override dependency %o", ctx->name);
			goto not_found;
		}
	}

	if (get_obj_type(wk, *ctx->res) != obj_dependency) {
		goto not_found;
	}

	*found = true;
	return true;
not_found:
	LLOG_I("%s", "");
	obj_fprintf(wk, log_file(), "fallback %o failed for %o\n", ctx->fallback, ctx->name);
	*ctx->res = 0;
	*found = false;
	return true;
}

static bool
get_dependency_pkgconfig(struct workspace *wk, struct dep_lookup_ctx *ctx, bool *found)
{
	struct pkgconf_info info = { 0 };
	*found = false;

	if (!muon_pkgconf_lookup(wk, ctx->name, ctx->lib_mode == dep_lib_mode_static, &info)) {
		return true;
	}

	obj ver_str = make_str(wk, info.version);
	bool ver_match;
	if (!check_dependency_version(wk, ver_str, ctx->err_node, ctx->versions->val, &ver_match)) {
		return false;
	} else if (!ver_match) {
		LLOG_I("%s", ""); // hack to print info before the next line
		obj_fprintf(wk, log_file(), "pkgconf found dependency %o, but the version %o does not match the requested version %o\n",
			ctx->name, ver_str, ctx->versions->val);
		return true;
	}

	make_obj(wk, ctx->res, obj_dependency);
	struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);
	dep->name = ctx->name;
	dep->version = ver_str;
	dep->flags |= dep_flag_found;
	dep->type = dependency_type_pkgconf;
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
		obj cached_dep;
		if (check_dependency_cache(wk, ctx, &cached_dep)) {
			bool ver_match;
			struct obj_dependency *dep = get_obj_dependency(wk, cached_dep);
			if (!check_dependency_version(wk, dep->version, ctx->versions->node,
				ctx->versions->val, &ver_match)) {
				return false;
			}

			if (!ver_match) {
				goto lookup_finished;
			}

			*ctx->res = cached_dep;
			found = true;
			ctx->from_cache = true;
			goto lookup_finished;
		}
	}

	if (check_dependency_override(wk, ctx, ctx->res)) {
		found = true;
		goto lookup_finished;
	}

	bool force_fallback = false;
	enum wrap_mode wrap_mode = get_option_wrap_mode(wk);

	if (!ctx->fallback) {
		obj provided_fallback;
		if (obj_dict_index(wk, current_project(wk)->wrap_provides_deps,
			ctx->name, &provided_fallback)) {
			ctx->fallback = provided_fallback;
		}
	}

	// implicitly fallback on a subproject named the same as this dependency
	if (!ctx->fallback && ctx->fallback_allowed) {
		make_obj(wk, &ctx->fallback, obj_array);
		obj_array_push(wk, ctx->fallback, ctx->name);
	}

	if (ctx->fallback) {
		obj force_fallback_for, subproj_name;

		get_option_value(wk, current_project(wk), "force_fallback_for", &force_fallback_for);
		obj_array_index(wk, ctx->fallback, 0, &subproj_name);

		force_fallback =
			wrap_mode == wrap_mode_forcefallback
			|| obj_array_in(wk, force_fallback_for, ctx->name)
			|| obj_dict_in(wk, wk->subprojects, subproj_name);
	}

	if (!found) {
		if (force_fallback) {
			if (!handle_dependency_fallback(wk, ctx, &found)) {
				return false;
			}
		} else {
			if (!get_dependency_pkgconfig(wk, ctx, &found)) {
				return false;
			}

			if (!found && ctx->fallback && wrap_mode != wrap_mode_nofallback) {
				if (!handle_dependency_fallback(wk, ctx, &found)) {
					return false;
				}
			}
		}
	}

lookup_finished:
	if (!found) {
		if (ctx->requirement == requirement_required) {
			LLOG_E("required ");
		} else {
			LLOG_W("%s", "");
		}

		obj_fprintf(wk, log_file(), "dependency %o not found", ctx->name);

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
			ctx->lib_mode == dep_lib_mode_static ? ", static" : "");
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
		dep->type = dependency_type_threads;

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
		kw_optional_modules, // ignored
		kw_fallback,
		kw_allow_fallback,
		kw_default_options,
		kw_not_found_message,
		kw_disabler,
		kw_method,
		kw_include_type,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required", tc_required_kw },
		[kw_native] = { "native", obj_bool },
		[kw_version] = { "version", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_static] = { "static", obj_bool },
		[kw_modules] = { "modules", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_optional_modules] = { "optional_modules", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_fallback] = { "fallback", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_allow_fallback] = { "allow_fallback", obj_bool },
		[kw_default_options] = { "default_options", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_not_found_message] = { "not_found_message", obj_string },
		[kw_disabler] = { "disabler", obj_bool },
		[kw_method] = { "method", obj_string },
		[kw_include_type] = { "include_type", obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum include_type inc_type = include_type_preserve;
	if (akw[kw_include_type].set) {
		if (!coerce_include_type(wk, get_str(wk, akw[kw_include_type].val),
			akw[kw_include_type].node, &inc_type)) {
			return false;
		}
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

	enum dep_lib_mode lib_mode = dep_lib_mode_default;
	if (akw[kw_static].set) {
		if (get_obj_bool(wk, akw[kw_static].val)) {
			lib_mode = dep_lib_mode_static;
		} else {
			lib_mode = dep_lib_mode_shared;
		}
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
	}

	struct dep_lookup_ctx ctx = {
		.res = res,
		.requirement = requirement,
		.versions = &akw[kw_version],
		.err_node = an[0].node,
		.fallback_node = fallback_err_node,
		.name = an[0].val,
		.fallback = fallback,
		.fallback_allowed = fallback_allowed,
		.default_options = &akw[kw_default_options],
		.not_found_message = akw[kw_not_found_message].val,
		.lib_mode = lib_mode,
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

	if (get_obj_type(wk, *res) == obj_dependency) {
		struct obj_dependency *dep = get_obj_dependency(wk, *res);

		if (ctx.from_cache) {
			obj dup;
			make_obj(wk, &dup, obj_dependency);
			struct obj_dependency *newdep = get_obj_dependency(wk, dup);
			*newdep = *dep;
			dep = newdep;
			*res = dup;
		}

		// set the include type if the return value is not a disabler
		dep->include_type = inc_type;

		if (dep->flags & dep_flag_found) {
			if (ctx.lib_mode != dep_lib_mode_shared) {
				obj_dict_set(wk, current_project(wk)->dep_cache.static_deps, ctx.name, *res);
			}

			if (ctx.lib_mode != dep_lib_mode_static) {
				obj_dict_set(wk, current_project(wk)->dep_cache.shared_deps, ctx.name, *res);
			}
		}
	}

	return true;
}

struct process_dependency_sources_ctx {
	uint32_t err_node;
	obj res;
};

static enum iteration_result
coerce_dependency_sources_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct process_dependency_sources_ctx *ctx = _ctx;

	switch (get_obj_type(wk, val)) {
	case obj_generated_list:
		obj_array_push(wk, ctx->res, val);
		break;
	default: {
		obj res;
		if (!coerce_files(wk, ctx->err_node, val, &res)) {
			return ir_err;
		}

		obj_array_extend_nodup(wk, ctx->res, res);
	}
	}

	return ir_cont;
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
		[kw_sources] = { "sources", ARG_TYPE_ARRAY_OF | tc_coercible_files },
		[kw_link_with] = { "link_with", tc_link_with_kw },
		[kw_link_whole] = { "link_whole", tc_link_with_kw },
		[kw_link_args] = { "link_args", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_dependencies] = { "dependencies", ARG_TYPE_ARRAY_OF | obj_any },
		[kw_version] = { "version", obj_string },
		[kw_include_directories] = { "include_directories", ARG_TYPE_ARRAY_OF | tc_coercible_inc },
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
	dep->flags |= dep_flag_found;
	dep->type = dependency_type_declared;
	dep->variables = akw[kw_variables].val;
	dep->deps = akw[kw_dependencies].val;
	dep->compile_args = akw[kw_compile_args].val;

	if (akw[kw_version].set) {
		dep->version = akw[kw_version].val;
	} else {
		dep->version = current_project(wk)->cfg.version;
	}

	if (akw[kw_sources].set) {
		make_obj(wk, &dep->sources, obj_array);

		struct process_dependency_sources_ctx ctx = {
			.err_node = akw[kw_sources].node,
			.res = dep->sources,
		};

		if (!obj_array_foreach_flat(wk, akw[kw_sources].val, &ctx, coerce_dependency_sources_iter)) {
			return false;
		}
	}

	make_obj(wk, &dep->link_with, obj_array);
	if (akw[kw_link_with].set) {
		obj_array_extend(wk, dep->link_with, akw[kw_link_with].val);
	}

	if (akw[kw_link_whole].set) {
		obj_array_extend(wk, dep->link_with, akw[kw_link_whole].val);
	}

	if (akw[kw_include_directories].set) {
		dep->include_directories = akw[kw_include_directories].val;
	}

	return true;
}
