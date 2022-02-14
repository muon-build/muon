#include "posix.h"

#include <string.h>

#include "external/libpkgconf.h"
#include "functions/build_target.h"
#include "functions/common.h"
#include "functions/dependency.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/path.h"

enum iteration_result
dep_args_includes_iter(struct workspace *wk, void *_ctx, obj inc_id)
{
	struct dep_args_ctx *ctx = _ctx;

	obj_array_push(wk, ctx->include_dirs, inc_id);
	return ir_cont;
}

enum iteration_result
dep_args_link_with_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct dep_args_ctx *ctx = _ctx;
	enum obj_type t = get_obj_type(wk, val);

	switch (t) {
	case  obj_build_target: {
		char path[PATH_MAX];
		struct obj_build_target *tgt = get_obj_build_target(wk, val);
		if (!tgt_build_path(wk, tgt, ctx->relativize, path)) {
			return ir_err;
		}

		obj_array_push(wk, ctx->link_with, make_str(wk, path));

		// calculate rpath for this target
		// we always want an absolute path here, regardles of
		// ctx->relativize
		if (tgt->type == tgt_dynamic_library) {
			char dir[PATH_MAX], abs[PATH_MAX];
			const char *p;
			if (!path_dirname(dir, PATH_MAX, path)) {
				return ir_err;
			}

			if (path_is_absolute(dir)) {
				p = dir;
			} else {
				if (!path_join(abs, PATH_MAX, wk->build_root, dir)) {
					return ir_err;
				}
				p = abs;
			}

			obj s = make_str(wk, p);

			if (!obj_array_in(wk, ctx->rpath, s)) {
				obj_array_push(wk, ctx->rpath, s);
			}
		}

		/* TODO: meson adds -I path/to/build/target.p, but why?
		 *	-- maybe for pch? */
		/* char tgt_parts_dir[PATH_MAX]; */
		/* if (!path_dirname(tgt_parts_dir, PATH_MAX, path)) { */
		/* 	return ir_err; */
		/* } else if (!path_add_suffix(tgt_parts_dir, PATH_MAX, ".p")) { */
		/* 	return ir_err; */
		/* } */

		/* obj_array_push(wk, ctx->include_dirs, make_str(wk, tgt_parts_dir)); */

		if (ctx->recursive && tgt->deps) {
			ctx->recursive = false;
			if (!obj_array_foreach(wk, tgt->deps, ctx, dep_args_iter)) {
				return ir_err;
			}
			ctx->recursive = true;
		}

		if (tgt->link_with) {
			if (!obj_array_foreach(wk, tgt->link_with, ctx, dep_args_link_with_iter)) {
				return ir_err;
			}
		}

		if (tgt->include_directories) {
			if (!obj_array_foreach_flat(wk, tgt->include_directories,
				ctx, dep_args_includes_iter)) {
				return ir_err;
			}
		}
		break;
	}
	case obj_custom_target: {
		obj_array_foreach(wk, get_obj_custom_target(wk, val)->output, ctx,
			dep_args_link_with_iter);
		break;
	}
	case obj_file: {
		obj str;

		if (ctx->relativize) {
			char path[PATH_MAX];
			if (!path_relative_to(path, PATH_MAX, wk->build_root, get_file_path(wk, val))) {
				return ir_err;
			}

			str = make_str(wk, path);
		} else {
			str = *get_obj_file(wk, val);
		}

		obj_array_push(wk, ctx->link_with, str);
		break;
	}
	case obj_string:
		obj_array_push(wk, ctx->link_with, val);
		break;
	default:
		LOG_E("invalid type for link_with: '%s'", obj_type_to_s(t));
		return ir_err;
	}

	return ir_cont;
}

enum iteration_result
dep_args_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct dep_args_ctx *ctx = _ctx;
	enum obj_type t = get_obj_type(wk, val);

	/* obj_fprintf(wk, log_file(), "%d|dep: %o\n", ctx->recursion_depth, val); */

	switch (t) {
	case obj_dependency: {
		struct obj_dependency *dep = get_obj_dependency(wk, val);

		if (!(dep->flags & dep_flag_found)) {
			return ir_cont;
		}

		if (dep->link_with) {
			bool was_recursive = ctx->recursive;
			if (was_recursive) {
				ctx->recursive = false;
			}
			if (!obj_array_foreach(wk, dep->link_with, _ctx, dep_args_link_with_iter)) {
				return ir_err;
			}
			if (was_recursive) {
				ctx->recursive = true;
			}
		}

		if (dep->link_with_not_found) {
			obj dup;
			obj_array_dup(wk, dep->link_with_not_found, &dup);
			obj_array_extend(wk, ctx->link_with_not_found, dup);
		}

		if (dep->include_directories) {
			if (!obj_array_foreach_flat(wk, dep->include_directories,
				_ctx, dep_args_includes_iter)) {
				return ir_err;
			}
		}

		if (dep->link_args) {
			obj dup;
			obj_array_dup(wk, dep->link_args, &dup);
			obj_array_extend(wk, ctx->link_args, dup);
		}

		if (dep->compile_args) {
			obj dup;
			obj_array_dup(wk, dep->compile_args, &dup);
			obj_array_extend(wk, ctx->compile_args, dup);
		}

		if (ctx->recursive && dep->deps) {
			if (!obj_array_foreach(wk, dep->deps, ctx, dep_args_iter)) {
				return ir_err;
			}
		}
		break;
	}
	case obj_external_library: {
		struct obj_external_library *lib = get_obj_external_library(wk, val);

		if (!lib->found) {
			return ir_cont;
		}

		obj_array_push(wk, ctx->link_with, lib->full_path);
		break;
	}
	default:
		LOG_E("invalid type for dependency: %s", obj_type_to_s(t));
		return ir_err;
	}

	return ir_cont;
}

void
dep_args_ctx_init(struct workspace *wk, struct dep_args_ctx *ctx)
{
	*ctx = (struct dep_args_ctx) { 0 };

	make_obj(wk, &ctx->include_dirs, obj_array);
	make_obj(wk, &ctx->link_with, obj_array);
	make_obj(wk, &ctx->link_with_not_found, obj_array);
	make_obj(wk, &ctx->link_args, obj_array);
	make_obj(wk, &ctx->compile_args, obj_array);
	make_obj(wk, &ctx->rpath, obj_array);
}

bool
deps_args(struct workspace *wk, obj deps, struct dep_args_ctx *ctx)
{
	return obj_array_foreach(wk, deps, ctx, dep_args_iter);
}

bool
dep_args(struct workspace *wk, obj dep, struct dep_args_ctx *ctx)
{
	return dep_args_iter(wk, ctx, dep) != ir_err;
}

static bool
func_dependency_found(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_bool);
	set_obj_bool(wk, *res,
		(get_obj_dependency(wk, rcvr)->flags & dep_flag_found) == dep_flag_found);
	return true;
}

static bool
dep_get_pkgconfig_variable(struct workspace *wk, obj dep, uint32_t node, obj var, obj *res)
{
	struct obj_dependency *d = get_obj_dependency(wk, dep);
	if (!(d->flags & dep_flag_pkg_config)) {
		interp_error(wk, node, "this dependency is not from pkg_config");
		return false;
	}

	if (!muon_pkgconf_get_variable(wk, get_cstr(wk, d->name), get_cstr(wk, var), res)) {
		return false;
	}
	return true;
}

static bool
func_dependency_get_pkgconfig_variable(struct workspace *wk, obj rcvr,
	uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_default,
	};
	struct args_kw akw[] = {
		[kw_default] = { "default", obj_string },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (!dep_get_pkgconfig_variable(wk, rcvr, an[0].node, an[0].val, res)) {
		if (akw[kw_default].set) {
			*res = akw[kw_default].val;
		} else {
			interp_error(wk, an[0].node, "undefined pkg_config variable");
			return false;
		}
	}

	return true;
}

static bool
func_dependency_get_variable(struct workspace *wk, obj rcvr,
	uint32_t args_node, obj *res)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_pkgconfig,
		kw_internal,
		kw_default_value,
	};
	struct args_kw akw[] = {
		[kw_pkgconfig] = { "pkgconfig", obj_string },
		[kw_internal] = { "internal", obj_string },
		[kw_default_value] = { "default_value", obj_string },
		0
	};
	if (!interp_args(wk, args_node, NULL, ao, akw)) {
		return false;
	}

	uint32_t node = args_node;

	if (ao[0].set) {
		node = ao[0].node;

		if (!akw[kw_pkgconfig].set) {
			akw[kw_pkgconfig].set = true;
			akw[kw_pkgconfig].node = ao[0].node;
			akw[kw_pkgconfig].val = ao[0].val;
		}

		if (!akw[kw_internal].set) {
			akw[kw_pkgconfig].set = true;
			akw[kw_pkgconfig].node = ao[0].node;
			akw[kw_pkgconfig].val = ao[0].val;
		}
	}

	struct obj_dependency *dep = get_obj_dependency(wk, rcvr);
	if (dep->flags & dep_flag_pkg_config) {
		if (akw[kw_pkgconfig].set) {
			node = akw[kw_pkgconfig].node;

			if (dep_get_pkgconfig_variable(wk, rcvr, akw[kw_pkgconfig].node, akw[kw_pkgconfig].val, res)) {
				return true;
			}
		}
	} else if (dep->variables) {
		if (akw[kw_internal].set) {
			node = akw[kw_internal].node;

			if (obj_dict_index(wk, dep->variables, akw[kw_internal].val, res)) {
				return true;
			}
		}
	}

	if (akw[kw_default_value].set) {
		*res = akw[kw_default_value].val;
		return true;
	} else {
		interp_error(wk, node, "undefined variable");
		return false;
	}
}

static bool
func_dependency_version(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	obj version = get_obj_dependency(wk, rcvr)->version;

	if (version) {
		*res = version;
	} else {
		*res = make_str(wk, "unknown");
	}

	return true;
}

static bool
func_dependency_type_name(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	struct obj_dependency *dep = get_obj_dependency(wk, rcvr);

	if (dep->flags & dep_flag_pkg_config) {
		*res = make_str(wk, "pkgconfig");
	} else {
		*res = make_str(wk, "internal");
	}

	return true;
}

const struct func_impl_name impl_tbl_dependency[] = {
	{ "found", func_dependency_found },
	{ "get_pkgconfig_variable", func_dependency_get_pkgconfig_variable },
	{ "get_variable", func_dependency_get_variable },
	{ "type_name", func_dependency_type_name },
	{ "version", func_dependency_version },
	{ NULL, NULL },
};
