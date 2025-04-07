/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "coerce.h"
#include "external/pkgconfig.h"
#include "functions/both_libs.h"
#include "functions/compiler.h"
#include "functions/file.h"
#include "functions/kernel/dependency.h"
#include "functions/kernel/subproject.h"
#include "functions/string.h"
#include "functions/subproject.h"
#include "lang/analyze.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "machines.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static const struct {
	const char *name;
	enum dependency_lookup_method method;
} dependency_lookup_method_names[] = {
	{ "auto", dependency_lookup_method_auto },
	{ "builtin", dependency_lookup_method_builtin },
	{ "cmake", dependency_lookup_method_cmake },
	{ "config-tool", dependency_lookup_method_config_tool },
	{ "dub", dependency_lookup_method_dub },
	{ "extraframework", dependency_lookup_method_extraframework },
	{ "pkg-config", dependency_lookup_method_pkgconfig },
	{ "sysconfig", dependency_lookup_method_sysconfig },
	{ "system", dependency_lookup_method_system },

	// For backwards compatibility
	{ "sdlconfig", dependency_lookup_method_config_tool },
	{ "cups-config", dependency_lookup_method_config_tool },
	{ "pcap-config", dependency_lookup_method_config_tool },
	{ "libwmf-config", dependency_lookup_method_config_tool },
	{ "qmake", dependency_lookup_method_config_tool },
};

bool
dependency_lookup_method_from_s(const struct str *s, enum dependency_lookup_method *lookup_method)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(dependency_lookup_method_names); ++i) {
		if (str_eql(s, &STRL(dependency_lookup_method_names[i].name))) {
			*lookup_method = dependency_lookup_method_names[i].method;
			break;
		}
	}

	if (i == ARRAY_LEN(dependency_lookup_method_names)) {
		return false;
	}

	return true;
}

const char *
dependency_lookup_method_to_s(enum dependency_lookup_method method)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(dependency_lookup_method_names); ++i) {
		if (dependency_lookup_method_names[i].method == method) {
			return dependency_lookup_method_names[i].name;
		}
	}

	UNREACHABLE;
}

enum dep_lib_mode {
	dep_lib_mode_default,
	dep_lib_mode_static,
	dep_lib_mode_shared,
};

struct dep_lookup_ctx {
	obj *res;
	struct args_kw *default_options, *versions, *handler_kwargs;
	enum requirement_type requirement;
	enum machine_kind machine;
	uint32_t err_node;
	uint32_t fallback_node;
	obj name;
	obj names;
	obj fallback;
	obj not_found_message;
	obj modules;
	enum dep_lib_mode lib_mode;
	enum dependency_lookup_method lookup_method;
	bool disabler;
	bool from_cache;
	bool found;
};

static obj
get_dependency_c_compiler(struct workspace *wk, enum machine_kind machine)
{
	struct project *proj = current_project(wk);

	obj compiler;

	if (obj_dict_geti(wk, proj->toolchains[machine], compiler_language_c, &compiler)) {
		return compiler;
	} else if (obj_dict_geti(wk, proj->toolchains[machine], compiler_language_cpp, &compiler)) {
		return compiler;
	} else if (obj_dict_geti(wk, proj->toolchains[machine], compiler_language_objc, &compiler)) {
		return compiler;
	} else if (obj_dict_geti(wk, proj->toolchains[machine], compiler_language_objcpp, &compiler)) {
		return compiler;
	} else {
		return 0;
	}
}

static bool
check_dependency_override(struct workspace *wk, struct dep_lookup_ctx *ctx)
{
	obj n;
	obj_array_for(wk, ctx->names, n) {
		if (ctx->lib_mode != dep_lib_mode_shared) {
			if (obj_dict_index(wk, wk->dep_overrides_static[ctx->machine], n, ctx->res)) {
				ctx->lib_mode = dep_lib_mode_static;
				ctx->found = true;
				break;
			}
		}

		if (ctx->lib_mode != dep_lib_mode_static) {
			if (obj_dict_index(wk, wk->dep_overrides_dynamic[ctx->machine], n, ctx->res)) {
				ctx->lib_mode = dep_lib_mode_shared;
				ctx->found = true;
				break;
			}
		}
	}

	if (ctx->found) {
		LO("found %o in override\n", ctx->name);
	}

	return ctx->found;
}

static bool
check_dependency_cache(struct workspace *wk, struct dep_lookup_ctx *ctx, obj *res)
{
	if (ctx->lib_mode != dep_lib_mode_shared) {
		if (obj_dict_index(wk, current_project(wk)->dep_cache.static_deps[ctx->machine], ctx->name, res)) {
			ctx->lib_mode = dep_lib_mode_static;
			return true;
		}
	}

	if (ctx->lib_mode != dep_lib_mode_static) {
		if (obj_dict_index(wk, current_project(wk)->dep_cache.shared_deps[ctx->machine], ctx->name, res)) {
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
	if (get_option_wrap_mode(wk) == wrap_mode_nofallback) {
		return true;
	}

	obj subproj_name, subproj_dep = 0, subproj;

	switch (get_obj_array(wk, ctx->fallback)->len) {
	case 2: subproj_dep = obj_array_index(wk, ctx->fallback, 1);
	/* FALLTHROUGH */
	case 1: subproj_name = obj_array_index(wk, ctx->fallback, 0); break;
	default: vm_error_at(wk, ctx->err_node, "expected array of length 1-2 for fallback"); return false;
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
			ctx->default_options->val = make_obj(wk, obj_array);
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

	if (!check_dependency_override(wk, ctx)) {
		if (subproj_dep) {
			if (!subproject_get_variable(wk, ctx->fallback_node, subproj_dep, 0, subproj, ctx->res)) {
				vm_warning_at(wk,
					ctx->fallback_node,
					"subproject dependency variable %o is not defined",
					subproj_dep);
				goto not_found;
			}
		} else {
			vm_warning_at(wk,
				ctx->fallback_node,
				"subproject does not override dependency %o for %s machine",
				ctx->name,
				machine_kind_to_s(ctx->machine));
			goto not_found;
		}
	}

	if (get_obj_type(wk, *ctx->res) != obj_dependency) {
		vm_warning_at(wk, ctx->fallback_node, "overridden dependency is not a dependency object");
		goto not_found;
	}

	struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);
	if (dep->machine != ctx->machine) {
		vm_warning_at(wk,
			ctx->fallback_node,
			"overridden dependency is for the %s machine, but a dependency for the %s machine was requested",
			machine_kind_to_s(dep->machine),
			machine_kind_to_s(ctx->machine));
		goto not_found;
	}

	*found = true;
	return true;
not_found:
	obj_lprintf(wk, "fallback %o failed for %o\n", ctx->fallback, ctx->name);
	*ctx->res = 0;
	*found = false;
	return true;
}

static bool
get_dependency_pkgconfig(struct workspace *wk, struct dep_lookup_ctx *ctx, bool *found)
{
	struct pkgconf_info info = { 0 };
	*found = false;

	if (!muon_pkgconf_lookup(wk,
		    get_dependency_c_compiler(wk, ctx->machine),
		    ctx->name,
		    ctx->lib_mode == dep_lib_mode_static,
		    &info)) {
		return true;
	}

	obj ver_str = make_str(wk, info.version);
	bool ver_match;
	if (!check_dependency_version(wk, ver_str, ctx->err_node, ctx->versions->val, &ver_match)) {
		return false;
	} else if (!ver_match) {
		obj_lprintf(wk,
			"pkgconf found dependency %o, but the version %o does not match the requested version %o\n",
			ctx->name,
			ver_str,
			ctx->versions->val);
		return true;
	}

	*ctx->res = make_obj(wk, obj_dependency);
	struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);
	dep->name = ctx->name;
	dep->version = ver_str;
	dep->flags |= dep_flag_found;
	dep->type = dependency_type_pkgconf;
	dep->dep.link_with = info.libs;
	dep->dep.link_with_not_found = info.not_found_libs;
	dep->dep.include_directories = info.includes;
	dep->dep.compile_args = info.compile_args;
	dep->dep.link_args = info.link_args;

	*found = true;
	return true;
}

static bool
get_dependency_appleframeworks(struct workspace *wk, struct dep_lookup_ctx *ctx, bool *found)
{
	if (host_machine.sys != machine_system_darwin) {
		return true;
	}

	obj modules;

	if (strcmp(get_cstr(wk, ctx->name), "appleframeworks") == 0) {
		if (!ctx->modules) {
			vm_error_at(wk, ctx->err_node, "'appleframeworks' dependency requires the modules keyword");
			return false;
		}

		modules = ctx->modules;
	} else {
		modules = make_obj(wk, obj_array);
		obj_array_push(wk, modules, ctx->name);
	}

	obj compiler = get_dependency_c_compiler(wk, ctx->machine);
	if (!compiler) {
		return true;
	}

	bool all_found = true;

	obj fw;
	obj_array_for(wk, modules, fw) {
		struct compiler_check_opts opts = {
			.mode = compiler_check_mode_link,
			.comp_id = compiler,
		};

		opts.args = make_obj(wk, obj_array);
		obj_array_push(wk, opts.args, make_str(wk, "-framework"));
		obj_array_push(wk, opts.args, fw);

		bool ok;
		const char *src = "int main(void) { return 0; }\n";
		if (!compiler_check(wk, &opts, src, 0, &ok)) {
			return false;
		}

		if (!ok) {
			all_found = false;
		}
	}

	*found = all_found;

	if (!*found) {
		return true;
	}

	*ctx->res = make_obj(wk, obj_dependency);

	struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);

	char name[512];
	obj_snprintf(wk, name, sizeof(name), "framework:%o", modules);

	dep->name = make_str(wk, name);
	dep->flags |= dep_flag_found;
	dep->type = dependency_type_appleframeworks;
	dep->dep.frameworks = make_obj(wk, obj_array);

	obj v;
	obj_array_for(wk, modules, v) {
		obj_array_push(wk, dep->dep.frameworks, v);
	}
	return true;
}

static bool
get_dependency_system(struct workspace *wk, struct dep_lookup_ctx *ctx, bool *found)
{
	obj compiler = get_dependency_c_compiler(wk, ctx->machine);
	if (!compiler) {
		return true;
	}
	enum find_library_flag flags = 0;

	if (ctx->lib_mode == dep_lib_mode_static) {
		flags = find_library_flag_only_static;
	}

	struct find_library_result find_result = find_library(wk, compiler, get_cstr(wk, ctx->name), 0, flags);

	if (!find_result.found) {
		return true;
	}

	*found = true;

	*ctx->res = make_obj(wk, obj_dependency);
	find_library_result_to_dependency(wk, find_result, compiler, *ctx->res);
	return true;
}

typedef bool (*native_dependency_lookup_handler)(struct workspace *, struct dep_lookup_ctx *, bool *);

static const native_dependency_lookup_handler dependency_lookup_handlers[] = {
	[dependency_lookup_method_pkgconfig] = get_dependency_pkgconfig,
	[dependency_lookup_method_builtin] = 0,
	[dependency_lookup_method_system] = get_dependency_system,
	[dependency_lookup_method_extraframework] = get_dependency_appleframeworks,
};

struct dependency_lookup_handler {
	enum dependency_lookup_handler_type {
		dependency_lookup_handler_type_native,
		dependency_lookup_handler_type_capture,
	} type;
	union {
		native_dependency_lookup_handler native;
		obj capture;
	} handler;
};

struct dependency_lookup_handlers {
	struct dependency_lookup_handler e[4];
	uint32_t len;
};

static void
dependency_lookup_handler_push_native(struct dependency_lookup_handlers *handlers, enum dependency_lookup_method m)
{
	assert(handlers->len < ARRAY_LEN(handlers->e));
	handlers->e[handlers->len].type = dependency_lookup_handler_type_native;
	handlers->e[handlers->len].handler.native = dependency_lookup_handlers[m];
	++handlers->len;
}

static void
dependency_lookup_handler_push_capture(struct dependency_lookup_handlers *handlers,
	enum dependency_lookup_method m,
	obj o)
{
	if (o == obj_bool_true) {
		dependency_lookup_handler_push_native(handlers, m);
	} else {
		assert(handlers->len < ARRAY_LEN(handlers->e));
		handlers->e[handlers->len].type = dependency_lookup_handler_type_capture;
		handlers->e[handlers->len].handler.capture = o;
		++handlers->len;
	}
}

static bool dependency_is_resolving_from_capture = false;

static bool
build_lookup_handler_list(struct workspace *wk, struct dep_lookup_ctx *ctx, struct dependency_lookup_handlers *handlers)
{
	obj handler_dict = 0;
	if (!dependency_is_resolving_from_capture
		&& obj_dict_index(wk, wk->dependency_handlers, ctx->name, &handler_dict)) {
		obj handler;

		if (ctx->lookup_method != dependency_lookup_method_auto) {
			if (!obj_dict_geti(wk, handler_dict, ctx->lookup_method, &handler)) {
				vm_error(wk,
					"Lookup method %s not supported for %o",
					dependency_lookup_method_to_s(ctx->lookup_method),
					ctx->name);
				return false;
			}

			dependency_lookup_handler_push_capture(handlers, ctx->lookup_method, handler);
		} else {
			obj method;
			obj_dict_for(wk, handler_dict, method, handler) {
				dependency_lookup_handler_push_capture(handlers, method, handler);
			}
		}
	} else {
		if (ctx->lookup_method == dependency_lookup_method_auto) {
			dependency_lookup_handler_push_native(handlers, dependency_lookup_method_pkgconfig);
			dependency_lookup_handler_push_native(handlers, dependency_lookup_method_extraframework);
		} else {
			if (!dependency_lookup_handlers[ctx->lookup_method]) {
				vm_error(wk,
					"Lookup method %s not supported for %o",
					dependency_lookup_method_to_s(ctx->lookup_method),
					ctx->name);
				return false;
			}

			dependency_lookup_handler_push_native(handlers, ctx->lookup_method);
		}
	}

	return true;
}

static obj
get_dependency_fallback_name(struct workspace *wk, struct dep_lookup_ctx *ctx)
{
	if (ctx->fallback) {
		return ctx->fallback;
	}

	obj provided_fallback;
	if (obj_dict_index(wk, current_project(wk)->wrap_provides_deps, ctx->name, &provided_fallback)) {
		return provided_fallback;
	}

	// implicitly fallback on a subproject named the same as this dependency
	obj res;
	res = make_obj(wk, obj_array);
	obj_array_push(wk, res, ctx->name);
	return res;
}

static bool
is_dependency_fallback_forced(struct workspace *wk, struct dep_lookup_ctx *ctx)
{
	obj force_fallback_for, subproj_name;

	get_option_value(wk, current_project(wk), "force_fallback_for", &force_fallback_for);
	subproj_name = obj_array_index(wk, get_dependency_fallback_name(wk, ctx), 0);

	enum wrap_mode wrap_mode = get_option_wrap_mode(wk);

	return wrap_mode == wrap_mode_forcefallback || obj_array_in(wk, force_fallback_for, ctx->name)
	       || obj_dict_in(wk, wk->subprojects, subproj_name);
}

static bool
get_dependency(struct workspace *wk, struct dep_lookup_ctx *ctx)
{
	{
		obj cached_dep;
		if (check_dependency_cache(wk, ctx, &cached_dep)) {
			if (ctx->found) {
				LO("found %o in cache\n", ctx->name);
			}

			bool ver_match;
			struct obj_dependency *dep = get_obj_dependency(wk, cached_dep);
			if (!check_dependency_version(
				    wk, dep->version, ctx->versions->node, ctx->versions->val, &ver_match)) {
				return false;
			}

			if (!ver_match) {
				return true;
			}

			*ctx->res = cached_dep;
			ctx->found = true;
			ctx->from_cache = true;
			return true;
		}
	}

	if (check_dependency_override(wk, ctx)) {
		return true;
	}

	if (is_dependency_fallback_forced(wk, ctx)) {
		// This dependency is forced to fall-back, handle it later.
		return true;
	}

	struct dependency_lookup_handlers handlers = { 0 };
	build_lookup_handler_list(wk, ctx, &handlers);

	uint32_t i;
	for (i = 0; i < handlers.len; ++i) {
		switch (handlers.e[i].type) {
		case dependency_lookup_handler_type_native:
			if (!handlers.e[i].handler.native(wk, ctx, &ctx->found)) {
				return false;
			}
			break;
		case dependency_lookup_handler_type_capture:
			stack_push(&wk->stack, dependency_is_resolving_from_capture, true);
			bool ok = vm_eval_capture(wk, handlers.e[i].handler.capture, 0, ctx->handler_kwargs, ctx->res);
			stack_pop(&wk->stack, dependency_is_resolving_from_capture);
			if (!ok) {
				return false;
			}
			struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);
			dep->name = ctx->name;
			if (dep->flags & dep_flag_found) {
				ctx->found = true;
			}
			break;
		}

		if (ctx->found) {
			break;
		}
	}

	if (!ctx->found && ctx->fallback) {
		if (!handle_dependency_fallback(wk, ctx, &ctx->found)) {
			return false;
		}
	}

	return true;
}

enum handle_special_dependency_result {
	handle_special_dependency_result_error,
	handle_special_dependency_result_continue,
	handle_special_dependency_result_stop,
};

static enum handle_special_dependency_result
handle_special_dependency(struct workspace *wk, struct dep_lookup_ctx *ctx)
{
	if (strcmp(get_cstr(wk, ctx->name), "threads") == 0) {
		LOG_I("dependency threads found");
		*ctx->res = make_obj(wk, obj_dependency);
		struct obj_dependency *dep = get_obj_dependency(wk, *ctx->res);
		dep->name = ctx->name;
		dep->flags |= dep_flag_found;
		dep->type = dependency_type_threads;

		dep->dep.compile_args = make_obj(wk, obj_array);
		obj_array_push(wk, dep->dep.compile_args, make_str(wk, "-pthread"));

		dep->dep.link_args = make_obj(wk, obj_array);
		obj_array_push(wk, dep->dep.link_args, make_str(wk, "-pthread"));
		ctx->found = true;
	} else if (strcmp(get_cstr(wk, ctx->name), "curses") == 0) {
		// TODO: this is stupid
		ctx->name = make_str(wk, "ncurses");
		if (!get_dependency(wk, ctx)) {
			return handle_special_dependency_result_error;
		}
	} else if (strcmp(get_cstr(wk, ctx->name), "appleframeworks") == 0) {
		get_dependency_appleframeworks(wk, ctx, &ctx->found);
	} else if (strcmp(get_cstr(wk, ctx->name), "") == 0) {
		if (ctx->requirement == requirement_required) {
			vm_error_at(wk, ctx->err_node, "dependency '' cannot be required");
			return handle_special_dependency_result_error;
		}
		*ctx->res = make_obj(wk, obj_dependency);
		ctx->found = true;
	} else {
		return handle_special_dependency_result_continue;
	}

	return handle_special_dependency_result_stop;
}

bool
func_dependency(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { TYPE_TAG_GLOB | obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_native,
		kw_version,
		kw_static,
		kw_modules,
		kw_optional_modules, // ignored
		kw_components, // ignored
		kw_main, // ignored
		kw_language, // ignored
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
		[kw_version] = { "version", TYPE_TAG_LISTIFY | obj_string },
		[kw_static] = { "static", obj_bool },
		[kw_modules] = {
			"modules",
			TYPE_TAG_LISTIFY | obj_string,
			.desc = "A list of sub-dependencies for this dependency.  Only supported by certain dependencies.",
		},
		[kw_main] = { "main", tc_bool, .desc = "Ignored" },
		[kw_optional_modules] = { "optional_modules", TYPE_TAG_LISTIFY | obj_string, .desc = "Ignored" },
		[kw_components] = { "components", TYPE_TAG_LISTIFY | obj_string, .desc = "Ignored" },
		[kw_language] = { "language", tc_string, .desc = "Ignored" },
		[kw_fallback] = { "fallback", TYPE_TAG_LISTIFY | obj_string },
		[kw_allow_fallback] = { "allow_fallback", obj_bool },
		[kw_default_options] = { "default_options", wk->complex_types.options_dict_or_list },
		[kw_not_found_message] = { "not_found_message", obj_string },
		[kw_disabler] = { "disabler", obj_bool },
		[kw_method] = { "method", obj_string },
		[kw_include_type] = { "include_type", obj_string },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (!get_obj_array(wk, an[0].val)->len) {
		vm_error_at(wk, an[0].node, "no dependency names specified");
		return false;
	}

	if (wk->vm.in_analyzer) {
		// TODO: check fallback keyword?
		obj name, _subproj;
		obj_array_for(wk, an[0].val, name) {
			if (get_str(wk, name)->len) {
				subproject(wk, name, requirement_auto, 0, 0, &_subproj);
			}
		}

		*res = make_typeinfo(wk, tc_dependency);
		return true;
	}

	enum requirement_type requirement;
	if (!coerce_requirement(wk, &akw[kw_required], &requirement)) {
		return false;
	}

	if (requirement == requirement_skip) {
		*res = make_obj(wk, obj_dependency);
		struct obj_dependency *dep = get_obj_dependency(wk, *res);
		dep->name = obj_array_index(wk, an[0].val, 0);
		return true;
	}

	if (!current_project(wk)) {
		vm_error(wk, "This function cannot be called before project()");
		return false;
	}

	enum include_type inc_type = include_type_preserve;
	if (akw[kw_include_type].set) {
		if (!coerce_include_type(
			    wk, get_str(wk, akw[kw_include_type].val), akw[kw_include_type].node, &inc_type)) {
			return false;
		}
	}

	enum dependency_lookup_method lookup_method = dependency_lookup_method_auto;
	if (akw[kw_method].set) {
		if (!dependency_lookup_method_from_s(get_str(wk, akw[kw_method].val), &lookup_method)) {
			vm_error_at(wk, akw[kw_method].node, "invalid dependency method %o", akw[kw_method].val);
			return false;
		}

		if (!(lookup_method == dependency_lookup_method_auto
			    || lookup_method == dependency_lookup_method_extraframework
			    || lookup_method == dependency_lookup_method_pkgconfig
			    || lookup_method == dependency_lookup_method_builtin
			    || lookup_method == dependency_lookup_method_system)) {
			vm_warning_at(wk,
				akw[kw_method].node,
				"unsupported dependency method %o, falling back to 'auto'",
				akw[kw_method].val);
			lookup_method = dependency_lookup_method_auto;
		}
	}

	enum dep_lib_mode lib_mode = dep_lib_mode_default;
	if (akw[kw_static].set) {
		if (get_obj_bool(wk, akw[kw_static].val)) {
			lib_mode = dep_lib_mode_static;
		} else {
			lib_mode = dep_lib_mode_shared;
		}
	} else {
		obj prefer_static;
		get_option_value(wk, current_project(wk), "prefer_static", &prefer_static);
		if (get_obj_bool(wk, prefer_static)) {
			lib_mode = dep_lib_mode_static;
		}
	}

	/* A fallback is allowed if */
	bool fallback_allowed = false;
	if (akw[kw_allow_fallback].set) {
		/* - allow_fallback: true */
		fallback_allowed = get_obj_bool(wk, akw[kw_allow_fallback].val);
	} else {
		/* - allow_fallback is not specified and the requirement is required */
		fallback_allowed = requirement == requirement_required
				   /* - allow_fallback is not specified and the fallback keyword is
		            *   specified with at least one value (i.e. not an empty array) */
				   || (akw[kw_fallback].set && get_obj_array(wk, akw[kw_fallback].val)->len);
	}

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

	enum machine_kind machine = coerce_machine_kind(wk, &akw[kw_native]);

	struct args_kw handler_kwargs[] = {
		{ "static", .val = lib_mode == dep_lib_mode_static ? obj_bool_true : obj_bool_false },
		{ "native", .val = akw[kw_native].set ? akw[kw_native].val : obj_bool_false },
		{ "modules", .val = akw[kw_modules].val },
		{ "main", .val = akw[kw_main].val },
		{ "required", .val = akw[kw_required].val },
		0,
	};

	struct dep_lookup_ctx ctx = {
		.res = res,
		.handler_kwargs = handler_kwargs,
		.names = an[0].val,
		.requirement = requirement,
		.machine = machine,
		.versions = &akw[kw_version],
		.err_node = an[0].node,
		.fallback_node = fallback_err_node,
		.fallback = fallback,
		.default_options = &akw[kw_default_options],
		.not_found_message = akw[kw_not_found_message].val,
		.lib_mode = lib_mode,
		.disabler = akw[kw_disabler].set && get_obj_bool(wk, akw[kw_disabler].val),
		.modules = akw[kw_modules].val,
		.lookup_method = lookup_method,
	};

	obj name;
	obj_array_for(wk, an[0].val, name) {
		struct dep_lookup_ctx sub_ctx = ctx;
		ctx.name = sub_ctx.name = name;

		switch (handle_special_dependency(wk, &sub_ctx)) {
		case handle_special_dependency_result_error: return false;
		case handle_special_dependency_result_stop: break;
		case handle_special_dependency_result_continue:
			if (!sub_ctx.found) {
				if (!get_dependency(wk, &sub_ctx)) {
					return false;
				}
			}
			break;
		}

		if (sub_ctx.found) {
			ctx.lib_mode = sub_ctx.lib_mode;
			ctx.from_cache = sub_ctx.from_cache;
			ctx.found = true;
			break;
		}
	}

	if (!ctx.found && fallback_allowed) {
		obj_array_for(wk, an[0].val, name) {
			struct dep_lookup_ctx sub_ctx = ctx;
			ctx.name = sub_ctx.name = name;

			sub_ctx.fallback = get_dependency_fallback_name(wk, &sub_ctx);

			if (!handle_dependency_fallback(wk, &sub_ctx, &sub_ctx.found)) {
				return false;
			}

			if (sub_ctx.found) {
				ctx.lib_mode = sub_ctx.lib_mode;
				ctx.from_cache = sub_ctx.from_cache;
				ctx.found = true;
				break;
			}
		}
	}

	if (!ctx.found) {
		if (ctx.requirement == requirement_required) {
			LLOG_E("required ");
		} else {
			LLOG_W("%s", "");
		}

		obj_lprintf(wk, "dependency %o not found", an[0].val);

		if (ctx.not_found_message) {
			obj_lprintf(wk, ", %#o", ctx.not_found_message);
		}

		log_plain("\n");

		if (ctx.requirement == requirement_required) {
			vm_error_at(wk, ctx.err_node, "required dependency not found");
			return false;
		} else {
			if (ctx.disabler) {
				*ctx.res = obj_disabler;
			} else {
				*ctx.res = make_obj(wk, obj_dependency);
				struct obj_dependency *dep = get_obj_dependency(wk, *ctx.res);
				dep->name = ctx.name;
				dep->type = dependency_type_not_found;
			}
		}
	} else if (!str_eql(get_str(wk, ctx.name), &STR(""))) {
		struct obj_dependency *dep = get_obj_dependency(wk, *ctx.res);

		if (!ctx.from_cache) {
			LLOG_I("found dependency ");
			if (dep->type == dependency_type_declared) {
				obj_lprintf(wk, "%o (declared dependency)", ctx.name);
			} else {
				log_plain("%s", get_cstr(wk, dep->name));
			}

			if (dep->version) {
				log_plain(" version %s", get_cstr(wk, dep->version));
			}

			if (ctx.lib_mode == dep_lib_mode_static) {
				log_plain(" static");
			}

			log_plain(" for the %s machine", machine_kind_to_s(machine));

			log_plain("\n");
		}
	}

	if (get_obj_type(wk, *res) == obj_dependency) {
		struct obj_dependency *dep = get_obj_dependency(wk, *res);

		if (ctx.from_cache) {
			obj dup;
			dup = make_obj(wk, obj_dependency);
			struct obj_dependency *newdep = get_obj_dependency(wk, dup);
			*newdep = *dep;
			dep = newdep;
			*res = dup;
		}

		// set the include type and machine if the return value is not a disabler
		dep->include_type = inc_type;
		dep->machine = machine;

		if (dep->flags & dep_flag_found && !ctx.from_cache) {
			obj name;
			obj_array_for(wk, ctx.names, name) {
				if (ctx.lib_mode != dep_lib_mode_shared) {
					obj_dict_set(wk,
						current_project(wk)->dep_cache.static_deps[machine],
						name,
						*ctx.res);
				}

				if (ctx.lib_mode != dep_lib_mode_static) {
					obj_dict_set(wk,
						current_project(wk)->dep_cache.shared_deps[machine],
						name,
						*ctx.res);
				}
			}
		}
	}

	return true;
}

struct process_dependency_sources_ctx {
	obj res;
};

static enum iteration_result
coerce_dependency_sources_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct process_dependency_sources_ctx *ctx = _ctx;

	switch (get_obj_type(wk, val)) {
	case obj_generated_list: obj_array_push(wk, ctx->res, val); break;
	default: {
		obj res;
		if (!coerce_files(wk, 0, val, &res)) {
			return ir_err;
		}

		obj_array_extend_nodup(wk, ctx->res, res);
	}
	}

	return ir_cont;
}

static bool
deps_determine_machine_list(struct workspace *wk, obj list, enum machine_kind *m)
{
	if (!list) {
		return false;
	}

	obj l;
	obj_array_for(wk, list, l) {
		switch (get_obj_type(wk, l)) {
		case obj_build_target: {
			struct obj_build_target *tgt = get_obj_build_target(wk, l);
			*m = tgt->machine;
			return true;
		}
		case obj_dependency: {
			struct obj_dependency *dep = get_obj_dependency(wk, l);
			if (!(dep->flags & dep_flag_found)) {
				continue;
			}

			switch (dep->type) {
			case dependency_type_pkgconf:
			case dependency_type_external_library:
			case dependency_type_appleframeworks: {
				*m = dep->machine;
				return true;
			}
			case dependency_type_declared: {
				if (deps_determine_machine_list(wk, dep->dep.raw.deps, m)) {
					return true;
				} else if (deps_determine_machine_list(wk, dep->dep.raw.link_with, m)) {
					return true;
				} else if (deps_determine_machine_list(wk, dep->dep.raw.link_whole, m)) {
					return true;
				}
				break;
			}
			default: break;
			}
			break;
		}
		default: break;
		}
	}

	return false;
}

static enum machine_kind
deps_determine_machine(struct workspace *wk, obj link_with, obj link_whole, obj deps)
{
	enum machine_kind m = machine_kind_host;
	if (deps_determine_machine_list(wk, deps, &m)) {
		return m;
	} else if (deps_determine_machine_list(wk, link_with, &m)) {
		return m;
	} else if (deps_determine_machine_list(wk, link_whole, &m)) {
		return m;
	}

	return machine_kind_host;
}

static bool
deps_check_machine_matches_list(struct workspace *wk, obj tgt_name, enum machine_kind tgt_machine, obj list)
{
	if (!list) {
		return true;
	}

	enum machine_kind machine;
	obj l;
	obj_array_for(wk, list, l) {
		switch (get_obj_type(wk, l)) {
		case obj_build_target: {
			struct obj_build_target *tgt = get_obj_build_target(wk, l);
			machine = tgt->machine;
			break;
		}
		case obj_dependency: {
			struct obj_dependency *dep = get_obj_dependency(wk, l);
			if (!(dep->flags & dep_flag_found)) {
				continue;
			}

			switch (dep->type) {
			case dependency_type_pkgconf:
			case dependency_type_external_library:
			case dependency_type_appleframeworks: break;
			case dependency_type_declared: {
				if (!deps_check_machine_matches(wk,
					    tgt_name,
					    tgt_machine,
					    dep->dep.raw.link_with,
					    dep->dep.raw.link_whole,
					    dep->dep.raw.deps)) {
					return false;
				}
				continue;
			}
			default: continue;
			}

			machine = dep->machine;
			break;
		}
		default: continue;
		}

		if (machine != tgt_machine) {
			vm_error(wk,
				"target %o is built for the %s machine while its dependency %o is built for the %s machine",
				tgt_name,
				machine_kind_to_s(tgt_machine),
				l,
				machine_kind_to_s(machine));
			return false;
		}
	}

	return true;
}

bool
deps_check_machine_matches(struct workspace *wk,
	obj tgt_name,
	enum machine_kind tgt_machine,
	obj link_with,
	obj link_whole,
	obj deps)
{
	return deps_check_machine_matches_list(wk, tgt_name, tgt_machine, link_with)
	       && deps_check_machine_matches_list(wk, tgt_name, tgt_machine, link_whole)
	       && deps_check_machine_matches_list(wk, tgt_name, tgt_machine, deps);
}

bool
func_declare_dependency(struct workspace *wk, obj _, obj *res)
{
	enum kwargs {
		kw_sources,
		kw_link_with,
		kw_link_whole,
		kw_link_args,
		kw_dependencies,
		kw_version,
		kw_include_directories,
		kw_variables,
		kw_compile_args,
		kw_objects,
		kw_extra_files,
	};
	struct args_kw akw[] = {
		[kw_sources] = { "sources", TYPE_TAG_LISTIFY | tc_coercible_files | tc_generated_list },
		[kw_link_with] = { "link_with", tc_link_with_kw },
		[kw_link_whole] = { "link_whole", tc_link_with_kw },
		[kw_link_args] = { "link_args", TYPE_TAG_LISTIFY | obj_string },
		[kw_dependencies] = { "dependencies", TYPE_TAG_LISTIFY | tc_dependency },
		[kw_version] = { "version", obj_string },
		[kw_include_directories] = { "include_directories", TYPE_TAG_LISTIFY | tc_coercible_inc },
		[kw_variables] = { "variables", tc_array | tc_dict },
		[kw_compile_args] = { "compile_args", TYPE_TAG_LISTIFY | obj_string },
		[kw_objects] = { "objects", TYPE_TAG_LISTIFY | tc_file | tc_string },
		[kw_extra_files] = { "extra_files", TYPE_TAG_LISTIFY | tc_coercible_files }, // ignored
		0,
	};

	if (!pop_args(wk, NULL, akw)) {
		return false;
	}

	if (akw[kw_include_directories].set) {
		obj inc_dirs;
		if (!coerce_include_dirs(
			    wk, akw[kw_include_directories].node, akw[kw_include_directories].val, false, &inc_dirs)) {
			return false;
		}

		akw[kw_include_directories].val = inc_dirs;
	}

	struct obj_dependency *dep;
	*res = make_obj(wk, obj_dependency);
	dep = get_obj_dependency(wk, *res);

	dep->name = make_strf(wk,
		"%s declared:%s",
		get_cstr(wk, current_project(wk)->cfg.name),
		get_str(wk, vm_inst_location_str(wk, wk->vm.ip - 1))->s);
	dep->flags |= dep_flag_found;
	dep->type = dependency_type_declared;

	struct build_dep_raw raw = {
		.deps = akw[kw_dependencies].val,
		.link_with = akw[kw_link_with].val,
		.link_whole = akw[kw_link_whole].val,
		.link_args = akw[kw_link_args].val,
		.compile_args = akw[kw_compile_args].val,
		.include_directories = akw[kw_include_directories].val,
		.objects = akw[kw_objects].val,
		.sources = akw[kw_sources].val,
	};

	if (!dependency_create(wk, &raw, &dep->dep, 0)) {
		return false;
	}

	if (akw[kw_variables].set
		&& !coerce_key_value_dict(wk, akw[kw_variables].node, akw[kw_variables].val, &dep->variables)) {
		return false;
	}

	if (akw[kw_version].set) {
		dep->version = akw[kw_version].val;
	} else {
		dep->version = current_project(wk)->cfg.version;
	}

	dep->machine
		= deps_determine_machine(wk, akw[kw_link_with].val, akw[kw_link_whole].val, akw[kw_dependencies].val);

	if (!deps_check_machine_matches(wk,
		    dep->name,
		    dep->machine,
		    akw[kw_link_with].val,
		    akw[kw_link_whole].val,
		    akw[kw_dependencies].val)) {
		return false;
	}

	return true;
}

/*
 */

static bool
skip_if_present(struct workspace *wk, obj arr, obj val)
{
	if (hash_get(&wk->vm.objects.obj_hash, &val)) {
		return true;
	}

	hash_set(&wk->vm.objects.obj_hash, &val, true);

	return false;
}

struct dep_process_includes_ctx {
	struct build_dep *dep;
	enum include_type include_type;
};

static enum iteration_result
dep_process_includes_iter(struct workspace *wk, void *_ctx, obj inc_id)
{
	struct dep_process_includes_ctx *ctx = _ctx;

	struct obj_include_directory *inc = get_obj_include_directory(wk, inc_id);

	bool new_is_system = inc->is_system;

	switch (ctx->include_type) {
	case include_type_preserve: break;
	case include_type_system: new_is_system = true; break;
	case include_type_non_system: new_is_system = false; break;
	}

	if (inc->is_system != new_is_system) {
		inc_id = make_obj(wk, obj_include_directory);
		struct obj_include_directory *new_inc = get_obj_include_directory(wk, inc_id);
		*new_inc = *inc;
		new_inc->is_system = new_is_system;
	}

	obj_array_push(wk, ctx->dep->include_directories, inc_id);
	return ir_cont;
}

void
dep_process_includes(struct workspace *wk, obj arr, enum include_type include_type, struct build_dep *dep)
{
	dep->raw.include_directories = arr;

	obj_array_foreach_flat(wk,
		arr,
		&(struct dep_process_includes_ctx){
			.include_type = include_type,
			.dep = dep,
		},
		dep_process_includes_iter);
}

void
build_dep_init(struct workspace *wk, struct build_dep *dep)
{
	if (!dep->include_directories) {
		dep->include_directories = make_obj(wk, obj_array);
	}

	if (!dep->link_with) {
		dep->link_with = make_obj(wk, obj_array);
	}

	if (!dep->link_whole) {
		dep->link_whole = make_obj(wk, obj_array);
	}

	if (!dep->link_with_not_found) {
		dep->link_with_not_found = make_obj(wk, obj_array);
	}

	if (!dep->frameworks) {
		dep->frameworks = make_obj(wk, obj_array);
	}

	if (!dep->link_args) {
		dep->link_args = make_obj(wk, obj_array);
	}

	if (!dep->compile_args) {
		dep->compile_args = make_obj(wk, obj_array);
	}

	if (!dep->order_deps) {
		dep->order_deps = make_obj(wk, obj_array);
	}

	if (!dep->rpath) {
		dep->rpath = make_obj(wk, obj_array);
	}

	if (!dep->sources) {
		dep->sources = make_obj(wk, obj_array);
	}

	if (!dep->objects) {
		dep->objects = make_obj(wk, obj_array);
	}
}

void
build_dep_merge(struct workspace *wk,
	struct build_dep *dest,
	const struct build_dep *src,
	enum build_dep_merge_flag flags)
{
	bool merge_all = flags & build_dep_merge_flag_merge_all;

	build_dep_init(wk, dest);

	dest->link_language = coalesce_link_languages(src->link_language, dest->link_language);

	if (src->link_with) {
		obj_array_extend(wk, dest->link_with, src->link_with);
	}

	if (src->link_with_not_found) {
		obj_array_extend(wk, dest->link_with_not_found, src->link_with_not_found);
	}

	if (src->link_whole) {
		obj_array_extend(wk, dest->link_whole, src->link_whole);
	}

	if (merge_all && src->include_directories) {
		obj_array_extend(wk, dest->include_directories, src->include_directories);
	}

	if (src->link_args) {
		obj_array_extend(wk, dest->link_args, src->link_args);
	}

	if (src->frameworks) {
		obj_array_extend(wk, dest->frameworks, src->frameworks);
	}

	if (merge_all && src->compile_args) {
		obj_array_extend(wk, dest->compile_args, src->compile_args);
	}

	if (src->rpath) {
		obj_array_extend(wk, dest->rpath, src->rpath);
	}

	if (src->order_deps) {
		obj_array_extend(wk, dest->order_deps, src->order_deps);
	}

	if (merge_all && src->sources) {
		obj_array_extend(wk, dest->sources, src->sources);
	}

	if (merge_all && src->objects) {
		obj_array_extend(wk, dest->objects, src->objects);
	}
}

static enum iteration_result
dedup_link_args_iter(struct workspace *wk, void *_ctx, obj val)
{
	obj new_args = *(obj *)_ctx;

	static const char *known[] = {
		"-pthread",
	};

	const char *s = get_cstr(wk, val);

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(known); ++i) {
		if (strcmp(known[i], s) == 0) {
			if (obj_array_in(wk, new_args, val)) {
				return ir_cont;
			} else {
				break;
			}
		}
	}

	obj_array_push(wk, new_args, val);
	return ir_cont;
}

static enum iteration_result
dedup_compile_args_iter(struct workspace *wk, void *_ctx, obj val)
{
	obj new_args = *(obj *)_ctx;

	const struct str *s = get_str(wk, val);

	if (str_eql(s, &STR("-pthread")) || str_startswith(s, &STR("-W")) || str_startswith(s, &STR("-D"))) {
		if (obj_array_in(wk, new_args, val)) {
			return ir_cont;
		}
	}

	obj_array_push(wk, new_args, val);
	return ir_cont;
}

static void
dedup_build_dep(struct workspace *wk, struct build_dep *dep)
{
	obj_array_dedup_in_place(wk, &dep->link_with);
	obj_array_dedup_in_place(wk, &dep->link_with_not_found);
	obj_array_dedup_in_place(wk, &dep->link_whole);
	obj_array_dedup_in_place(wk, &dep->frameworks);
	obj_array_dedup_in_place(wk, &dep->raw.deps);
	obj_array_dedup_in_place(wk, &dep->raw.order_deps);
	obj_array_dedup_in_place(wk, &dep->raw.link_with);
	obj_array_dedup_in_place(wk, &dep->raw.link_whole);
	obj_array_dedup_in_place(wk, &dep->include_directories);
	obj_array_dedup_in_place(wk, &dep->rpath);
	obj_array_dedup_in_place(wk, &dep->order_deps);
	obj_array_dedup_in_place(wk, &dep->sources);
	obj_array_dedup_in_place(wk, &dep->objects);

	obj new_link_args;
	new_link_args = make_obj(wk, obj_array);
	obj_array_foreach(wk, dep->link_args, &new_link_args, dedup_link_args_iter);
	dep->link_args = new_link_args;

	obj new_compile_args;
	new_compile_args = make_obj(wk, obj_array);
	obj_array_foreach(wk, dep->compile_args, &new_compile_args, dedup_compile_args_iter);
	dep->compile_args = new_compile_args;
}

struct dep_process_link_with_ctx {
	struct build_dep *dest;
	bool link_whole;
	enum build_dep_flag flags;
};

static bool
dep_process_link_with_lib(struct workspace *wk, struct dep_process_link_with_ctx *ctx, obj val)
{
	if (skip_if_present(wk, ctx->dest->raw.link_with, val)) {
		return true;
	}

	enum obj_type t = get_obj_type(wk, val);

	/* obj_lprintf(wk, "link_with: %o\n", val); */

	obj dest_link_with;

	if (ctx->link_whole) {
		dest_link_with = ctx->dest->link_whole;
	} else {
		dest_link_with = ctx->dest->link_with;
	}

	switch (t) {
	case obj_both_libs: {
		enum default_both_libraries def_both_libs = default_both_libraries_auto;

		if (ctx->flags & build_dep_flag_both_libs_shared) {
			def_both_libs = default_both_libraries_shared;
		} else if (ctx->flags & build_dep_flag_both_libs_static) {
			def_both_libs = default_both_libraries_static;
		}

		if (def_both_libs != default_both_libraries_auto) {
			struct obj_both_libs *b = get_obj_both_libs(wk, val);
			switch (def_both_libs) {
			case default_both_libraries_auto: val = b->dynamic_lib; break;
			case default_both_libraries_static: val = b->static_lib; break;
			case default_both_libraries_shared: val = b->dynamic_lib; break;
			}
		} else if (ctx->link_whole) {
			struct obj_both_libs *b = get_obj_both_libs(wk, val);
			val = b->static_lib;
		} else {
			val = decay_both_libs(wk, val);
		}
	}
	/* fallthrough */
	case obj_build_target: {
		const struct obj_build_target *tgt = get_obj_build_target(wk, val);
		obj link_to = tgt->build_path;
		if (tgt->implib) {
			link_to = tgt->implib;
		}

		const char *path = get_cstr(wk, link_to);

		if (ctx->link_whole && tgt->type != tgt_static_library) {
			vm_error(wk, "link whole only accepts static libraries");
			return false;
		}

		if (tgt->type != tgt_executable) {
			obj_array_push(wk, dest_link_with, make_str(wk, path));
		}

		// calculate rpath for this target
		// we always want an absolute path here, regardles of
		// ctx->relativize
		if (tgt->type != tgt_static_library) {
			TSTR(abs);
			TSTR(dir);
			const char *p;
			path_dirname(wk, &dir, path);

			if (path_is_absolute(dir.buf)) {
				p = dir.buf;
			} else {
				path_join(wk, &abs, wk->build_root, dir.buf);
				p = abs.buf;
			}

			obj s = make_str(wk, p);

			if (!obj_array_in(wk, ctx->dest->rpath, s)) {
				obj_array_push(wk, ctx->dest->rpath, s);
			}
		}

		build_dep_merge(wk, ctx->dest, &tgt->dep, 0);
		break;
	}
	case obj_custom_target: {
		obj v;
		obj_array_for(wk, get_obj_custom_target(wk, val)->output, v) {
			if (!dep_process_link_with_lib(wk, ctx, v)) {
				return false;
			}
		}
		break;
	}
	case obj_file: {
		obj_array_push(wk, dest_link_with, *get_obj_file(wk, val));
		if (file_is_dynamic_lib(wk, val)) {
			TSTR(dir);
			path_dirname(wk, &dir, get_file_path(wk, val));
			obj_array_push(wk, ctx->dest->rpath, tstr_into_str(wk, &dir));
		}
		break;
	}
	case obj_string: obj_array_push(wk, dest_link_with, val); break;
	default: {
		vm_error(wk, "invalid type for link_with: '%s'", obj_type_to_s(t));
		return false;
	}
	}

	return true;
}

static bool
dep_process_link_with(struct workspace *wk, obj arr, struct build_dep *dest, enum build_dep_flag flags, bool link_whole)
{
	if (link_whole) {
		dest->raw.link_whole = arr;
	} else {
		dest->raw.link_with = arr;
	}

	build_dep_init(wk, dest);

	hash_clear(&wk->vm.objects.obj_hash);

	struct dep_process_link_with_ctx ctx = {
		.dest = dest,
		.flags = flags,
		.link_whole = link_whole,
	};

	obj v;
	obj_array_flat_for_(wk, arr, v, iter) {
		if (!dep_process_link_with_lib(wk, &ctx, v)) {
			obj_array_flat_iter_end(wk, &iter);
			return false;
		}
	}

	dedup_build_dep(wk, dest);
	return true;
}

void
dep_process_deps(struct workspace *wk, obj deps, struct build_dep *dest)
{
	build_dep_init(wk, dest);
	dest->raw.deps = deps;

	hash_clear(&wk->vm.objects.obj_hash);

	obj val;
	obj_array_for(wk, deps, val) {
		if (skip_if_present(wk, dest->raw.deps, val)) {
			continue;
		}

		const struct obj_dependency *dep = get_obj_dependency(wk, val);
		if (!(dep->flags & dep_flag_found)) {
			continue;
		}

		build_dep_merge(wk, dest, &dep->dep, build_dep_merge_flag_merge_all);
	}

	dedup_build_dep(wk, dest);
}

obj
dependency_dup(struct workspace *wk, obj dep, enum build_dep_flag flags)
{
	const struct obj_dependency *src = get_obj_dependency(wk, dep);

	obj res = make_obj(wk, obj_dependency);
	struct obj_dependency *d = get_obj_dependency(wk, res);
	*d = *src;
	d->dep = (struct build_dep){ 0 };

	// Copy everything over that can't be recreated from the raw field
	d->dep.link_language = src->dep.link_language;
	if (src->dep.frameworks) {
		obj_array_dup(wk, src->dep.frameworks, &d->dep.frameworks);
	}

	if (!dependency_create(wk, &src->dep.raw, &d->dep, flags)) {
		return 0;
	}

	return res;
}

bool
dependency_create(struct workspace *wk,
	const struct build_dep_raw *raw,
	struct build_dep *dep,
	enum build_dep_flag flags)
{
#define IS_INCLUDED(__part) (!partial || (flags & build_dep_flag_part_##__part))

	const enum build_dep_flag both_libs_mask = build_dep_flag_both_libs_static | build_dep_flag_both_libs_shared;
	const enum build_dep_flag part_mask = build_dep_flag_partial | build_dep_flag_part_compile_args
					      | build_dep_flag_part_includes | build_dep_flag_part_link_args
					      | build_dep_flag_part_links | build_dep_flag_part_sources;
	if (raw->flags & both_libs_mask) {
		flags &= ~both_libs_mask;
		flags |= (raw->flags & both_libs_mask);
	}
	flags |= (raw->flags & part_mask);

	dep->raw = *raw;
	dep->raw.flags = flags;

	build_dep_init(wk, dep);

	bool partial = flags & build_dep_flag_partial;

	if (raw->objects && !partial) {
		obj_array_extend_nodup(wk, dep->objects, raw->objects);
	}

	if (raw->link_args && IS_INCLUDED(link_args)) {
		obj_array_extend_nodup(wk, dep->link_args, raw->link_args);
	}

	if (raw->compile_args && IS_INCLUDED(compile_args)) {
		obj_array_extend_nodup(wk, dep->compile_args, raw->compile_args);
	}

	if (raw->sources && IS_INCLUDED(sources)) {
		struct process_dependency_sources_ctx ctx = {
			.res = dep->sources,
		};

		if (!obj_array_foreach_flat(wk, raw->sources, &ctx, coerce_dependency_sources_iter)) {
			return false;
		}
	}

	if (raw->link_with && IS_INCLUDED(links)) {
		bool link_whole = flags & build_dep_flag_as_link_whole;
		if (!dep_process_link_with(wk, raw->link_with, dep, flags, link_whole)) {
			return false;
		}
	}

	if (raw->link_whole && IS_INCLUDED(links)) {
		if (!dep_process_link_with(wk, raw->link_whole, dep, 0, true)) {
			return false;
		}
	}

	if (raw->link_with_not_found && IS_INCLUDED(links)) {
		obj_array_extend_nodup(wk, dep->link_with_not_found, raw->link_with_not_found);
	}

	if (raw->rpath && IS_INCLUDED(links)) {
		obj_array_extend_nodup(wk, dep->rpath, raw->rpath);
	}

	if (raw->include_directories && IS_INCLUDED(includes)) {
		enum include_type inc_type = include_type_preserve;
		if (flags & build_dep_flag_include_system) {
			inc_type = include_type_system;
		} else if (flags & build_dep_flag_include_non_system) {
			inc_type = include_type_non_system;
		}

		dep_process_includes(wk, raw->include_directories, inc_type, dep);
	}

	if (raw->deps) {
		obj raw_deps = raw->deps;

		if (flags & build_dep_flag_recursive) {
			obj recreated_deps = make_obj(wk, obj_array);

			obj val, dup;
			obj_array_for(wk, raw->deps, val) {
				if (!(dup = dependency_dup(wk, val, flags))) {
					return false;
				}

				obj_array_push(wk, recreated_deps, dup);
			}

			raw_deps = recreated_deps;
		}

		dep_process_deps(wk, raw_deps, dep);
	}

	return true;

#undef IS_INCLUDED
}
