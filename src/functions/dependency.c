#include "posix.h"

#include <string.h>

#include "coerce.h"
#include "external/libpkgconf.h"
#include "functions/build_target.h"
#include "functions/common.h"
#include "functions/dependency.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/path.h"

static enum iteration_result
dep_args_includes_iter(struct workspace *wk, void *_ctx, obj inc_id)
{
	struct dep_args_ctx *ctx = _ctx;

	struct obj_include_directory *inc = get_obj_include_directory(wk, inc_id);

	bool new_is_system = inc->is_system;

	switch (ctx->include_type) {
	case include_type_preserve:
		break;
	case include_type_system:
		new_is_system = true;
		break;
	case include_type_non_system:
		new_is_system = false;
		break;
	}

	if (inc->is_system != new_is_system) {
		make_obj(wk, &inc_id, obj_include_directory);
		struct obj_include_directory *new_inc =
			get_obj_include_directory(wk, inc_id);
		*new_inc = *inc;
		new_inc->is_system = new_is_system;
	}

	obj_array_push(wk, ctx->include_dirs, inc_id);
	return ir_cont;
}

static enum iteration_result dep_args_iter(struct workspace *wk, void *_ctx, obj val);

static enum iteration_result
dep_args_link_with_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct dep_args_ctx *ctx = _ctx;
	enum obj_type t = get_obj_type(wk, val);

	/* obj_fprintf(wk, log_file(), "%d|lw-dep: %o\n", ctx->recursion_depth, val); */

	switch (t) {
	case obj_build_target: {
		struct obj_build_target *tgt = get_obj_build_target(wk, val);
		const char *path = get_cstr(wk, tgt->build_path);
		char rel[PATH_MAX];
		if (ctx->relativize) {
			if (!path_relative_to(rel, PATH_MAX, wk->build_root, path)) {
				return false;
			}

			path = rel;
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

		if (ctx->recursive && tgt->deps) {
			if (!obj_array_foreach(wk, tgt->deps, ctx, dep_args_iter)) {
				return ir_err;
			}
		}

		if (tgt->link_with) {
			if (!obj_array_foreach(wk, tgt->link_with, ctx, dep_args_link_with_iter)) {
				return ir_err;
			}
		}

		if (get_obj_array(wk, tgt->order_deps)->len) {
			obj_array_extend(wk, ctx->order_deps, tgt->order_deps);
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

static enum iteration_result
dep_args_iter(struct workspace *wk, void *_ctx, obj val)
{
	if (hash_get(&wk->obj_hash, &val)) {
		return ir_cont;
	}
	hash_set(&wk->obj_hash, &val, true);

	struct dep_args_ctx *ctx = _ctx;
	enum obj_type t = get_obj_type(wk, val);

	/* obj_fprintf(wk, log_file(), "%d|dep: %o\n", ctx->recursion_depth, val); */

	switch (t) {
	case obj_dependency: {
		struct obj_dependency *dep = get_obj_dependency(wk, val);
		enum dep_flags old_parts = ctx->parts;
		ctx->parts = dep->flags & dep_flag_parts;

		if (!(dep->flags & dep_flag_found)) {
			return ir_cont;
		}

		if (dep->link_with && !(ctx->parts & dep_flag_no_links)) {
			if (!obj_array_foreach(wk, dep->link_with, _ctx, dep_args_link_with_iter)) {
				return ir_err;
			}
		}

		if (dep->link_with_not_found && !(ctx->parts & dep_flag_no_links)) {
			obj_array_extend(wk, ctx->link_with_not_found, dep->link_with_not_found);
		}

		if (dep->include_directories && !(ctx->parts & dep_flag_no_includes)) {
			ctx->include_type = dep->include_type;

			if (!obj_array_foreach_flat(wk, dep->include_directories,
				ctx, dep_args_includes_iter)) {
				return ir_err;
			}
		}

		if (dep->link_args && !(ctx->parts & dep_flag_no_link_args)) {
			obj_array_extend(wk, ctx->link_args, dep->link_args);
		}

		if (dep->compile_args && !(ctx->parts & dep_flag_no_compile_args)) {
			obj_array_extend(wk, ctx->compile_args, dep->compile_args);
		}

		if (ctx->recursive && dep->deps) {
			if (!obj_array_foreach(wk, dep->deps, ctx, dep_args_iter)) {
				return ir_err;
			}
		}

		ctx->parts = old_parts;
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
	make_obj(wk, &ctx->order_deps, obj_array);
	make_obj(wk, &ctx->rpath, obj_array);
}

bool
deps_args_link_with_only(struct workspace *wk, obj link_with, struct dep_args_ctx *ctx)
{
	hash_clear(&wk->obj_hash);

	return obj_array_foreach(wk, link_with, ctx, dep_args_link_with_iter);
}

bool
deps_args(struct workspace *wk, obj deps, struct dep_args_ctx *ctx)
{
	hash_clear(&wk->obj_hash);

	return obj_array_foreach(wk, deps, ctx, dep_args_iter);
}

bool
dep_args(struct workspace *wk, obj dep, struct dep_args_ctx *ctx)
{
	hash_clear(&wk->obj_hash);

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
	if (d->type != dependency_type_pkgconf) {
		interp_error(wk, node, "dependency not from pkgconf");
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
			akw[kw_internal].set = true;
			akw[kw_internal].node = ao[0].node;
			akw[kw_internal].val = ao[0].val;
		}
	}

	struct obj_dependency *dep = get_obj_dependency(wk, rcvr);
	if (dep->type == dependency_type_pkgconf) {
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

	if (dep->type == dependency_type_pkgconf) {
		*res = make_str(wk, "pkgconfig");
	} else {
		*res = make_str(wk, "internal");
	}

	return true;
}

static bool
func_dependency_partial_dependency(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	enum kwargs {
		kw_compile_args,
		kw_includes,
		kw_link_args,
		kw_links,
		kw_sources,
	};
	struct args_kw akw[] = {
		[kw_compile_args] = { "compile_args", obj_bool },
		[kw_includes] = { "includes", obj_bool },
		[kw_link_args] = { "link_args", obj_bool },
		[kw_links] = { "links", obj_bool },
		[kw_sources] = { "sources", obj_bool },
		0
	};
	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	const enum dep_flags part[] = {
		[kw_compile_args] = dep_flag_no_compile_args,
		[kw_includes] = dep_flag_no_includes,
		[kw_link_args] = dep_flag_no_link_args,
		[kw_links] = dep_flag_no_links,
		[kw_sources] = dep_flag_no_sources,
		0
	};

	struct obj_dependency *dep = get_obj_dependency(wk, rcvr);
	enum dep_flags exclude = dep->flags & dep_flag_parts;
	enum kwargs kw;
	for (kw = 0; part[kw]; ++kw) {
		if (!(akw[kw].set && get_obj_bool(wk, akw[kw].val))) {
			exclude |= part[kw];
		}
	}

	make_obj(wk, res, obj_dependency);
	struct obj_dependency *partial = get_obj_dependency(wk, *res);

	*partial = *dep;
	partial->flags = (dep->flags & ~dep_flag_parts) | exclude;
	return true;
}

static bool
func_dependency_as_system(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, NULL, ao, NULL)) {
		return false;
	}

	enum include_type inc_type = include_type_system;
	if (ao[0].set) {
		if (!coerce_include_type(wk, get_str(wk, ao[0].val), ao[0].node, &inc_type)) {
			return false;
		}
	}

	make_obj(wk, res, obj_dependency);

	struct obj_dependency *dep = get_obj_dependency(wk, *res);

	*dep = *get_obj_dependency(wk, rcvr);
	dep->include_type = inc_type;

	return true;
}

static bool
func_dependency_include_type(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *s = NULL;
	switch (get_obj_dependency(wk, rcvr)->include_type) {
	case include_type_preserve:
		s = "preserve";
		break;
	case include_type_system:
		s = "system";
		break;
	case include_type_non_system:
		s = "non-system";
		break;
	default:
		assert(false && "unreachable");
		break;
	}

	*res = make_str(wk, s);
	return true;
}

const struct func_impl_name impl_tbl_dependency[] = {
	{ "as_system", func_dependency_as_system },
	{ "found", func_dependency_found },
	{ "get_pkgconfig_variable", func_dependency_get_pkgconfig_variable },
	{ "get_variable", func_dependency_get_variable },
	{ "include_type", func_dependency_include_type },
	{ "partial_dependency", func_dependency_partial_dependency },
	{ "type_name", func_dependency_type_name },
	{ "version", func_dependency_version },
	{ NULL, NULL },
};
