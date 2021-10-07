#include "posix.h"

#include <string.h>

#include "external/pkgconf.h"
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
	assert(get_obj(wk, inc_id)->type == obj_file);

	obj path;
	make_obj(wk, &path, obj_string)->dat.str = get_obj(wk, inc_id)->dat.file;
	obj_array_push(wk, ctx->include_dirs, path);

	return ir_cont;
}

enum iteration_result
dep_args_link_with_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct dep_args_ctx *ctx = _ctx;
	struct obj *tgt = get_obj(wk, val_id);

	switch (tgt->type) {
	case  obj_build_target: {
		char path[PATH_MAX];
		if (!tgt_build_path(wk, tgt, ctx->relativize, path)) {
			return ir_err;
		}

		obj_array_push(wk, ctx->link_with, make_str(wk, path));

		/* TODO: meson adds -I path/to/build/target.p, but why? */
		/* char tgt_parts_dir[PATH_MAX]; */
		/* if (!path_dirname(tgt_parts_dir, PATH_MAX, path)) { */
		/* 	return ir_err; */
		/* } else if (!path_add_suffix(tgt_parts_dir, PATH_MAX, ".p")) { */
		/* 	return ir_err; */
		/* } */

		/* obj_array_push(wk, ctx->include_dirs, make_str(wk, tgt_parts_dir)); */

		if (ctx->recursive && tgt->dat.tgt.deps) {
			if (!obj_array_foreach(wk, tgt->dat.tgt.deps, ctx, dep_args_iter)) {
				return ir_err;
			}
		}

		if (tgt->dat.tgt.include_directories) {
			if (!obj_array_foreach_flat(wk, tgt->dat.tgt.include_directories,
				ctx, dep_args_includes_iter)) {
				return ir_err;
			}
		}
		break;
	}
	case obj_string:
		obj_array_push(wk, ctx->link_with, val_id);
		break;
	default:
		LOG_E("invalid type for link_with: '%s'", obj_type_to_s(tgt->type));
		return ir_err;
	}

	return ir_cont;
}

enum iteration_result
dep_args_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct dep_args_ctx *ctx = _ctx;
	struct obj *dep = get_obj(wk, val);

	switch (dep->type) {
	case obj_dependency:
		if (dep->dat.dep.link_with) {
			if (!obj_array_foreach(wk, dep->dat.dep.link_with, _ctx, dep_args_link_with_iter)) {
				return ir_err;
			}
		}

		if (dep->dat.dep.include_directories) {
			if (!obj_array_foreach_flat(wk, dep->dat.dep.include_directories,
				_ctx, dep_args_includes_iter)) {
				return ir_err;
			}
		}

		if (dep->dat.dep.link_args) {
			obj dup;
			obj_array_dup(wk, dep->dat.dep.link_args, &dup);
			obj_array_extend(wk, ctx->link_args, dup);
		}
		break;
	case obj_external_library: {
		obj val;
		make_obj(wk, &val, obj_string)->dat.str = dep->dat.external_library.full_path;
		obj_array_push(wk, ctx->link_with, val);
		break;
	}
	default:
		LOG_E("invalid type for dependency: %s", obj_type_to_s(dep->type));
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
	make_obj(wk, &ctx->link_args, obj_array);
	make_obj(wk, &ctx->args_dict, obj_dict);
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
dep_get_pkgconfig_variable(struct workspace *wk, uint32_t dep, uint32_t node, uint32_t var, uint32_t *obj)
{
	if (!(get_obj(wk, dep)->dat.dep.flags & dep_flag_pkg_config)) {
		interp_error(wk, node, "this dependency is not from pkg_config");
		return false;
	}

	uint32_t res;
	if (!muon_pkgconf_get_variable(wk, get_cstr(wk, get_obj(wk, dep)->dat.dep.name), get_cstr(wk, var), &res)) {
		interp_error(wk, node, "undefined pkg_config variable");
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = res;
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

	return dep_get_pkgconfig_variable(wk, rcvr, an[0].node, an[0].val, obj);
}

static bool
func_dependency_get_variable(struct workspace *wk, uint32_t rcvr,
	uint32_t args_node, obj *res)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_pkgconfig,
	};
	struct args_kw akw[] = {
		[kw_pkgconfig] = { "pkgconfig", obj_string },
		0
	};
	if (!interp_args(wk, args_node, NULL, ao, akw)) {
		return false;
	}

	struct obj *dep = get_obj(wk, rcvr);
	if (ao[0].set) {
		if (dep->dat.dep.variables) {
			if (!obj_dict_index(wk, dep->dat.dep.variables, ao[0].val, res)) {
				interp_error(wk, ao[0].node, "undefined variable");
				return false;
			}
			return true;
		} else {
			return dep_get_pkgconfig_variable(wk, rcvr, akw[kw_pkgconfig].node, ao[0].val, res);
		}
	} else if (akw[kw_pkgconfig].set) {
		return dep_get_pkgconfig_variable(wk, rcvr, akw[kw_pkgconfig].node, akw[kw_pkgconfig].val, res);
	} else {
		interp_error(wk, args_node, "I don't know how to get this type of variable");
		return false;
	}
}

static bool
func_dependency_version(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	obj version = get_obj(wk, rcvr)->dat.dep.version;

	if (version) {
		*res = version;
	} else {
		make_obj(wk, res, obj_string)->dat.str = wk_str_push(wk, "unknown");
	}

	return true;
}

const struct func_impl_name impl_tbl_dependency[] = {
	{ "found", func_dependency_found },
	{ "get_pkgconfig_variable", func_dependency_get_pkgconfig_variable },
	{ "get_variable", func_dependency_get_variable },
	{ "version", func_dependency_version },
	{ NULL, NULL },
};
