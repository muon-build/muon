#include "posix.h"

#include <string.h>

#include "log.h"
#include "run_cmd.h"
#include "interpreter.h"
#include "functions/common.h"
#include "coerce.h"

bool
pkg_config(struct workspace *wk, struct run_cmd_ctx *ctx, uint32_t args_node, const char *arg, const char *depname)
{
	if (!run_cmd(ctx, "pkg-config", (char *[]){ "pkg-config", (char *)arg, (char *)depname, NULL })) {
		if (ctx->err_msg) {
			interp_error(wk, args_node, "error: %s", ctx->err_msg);
		} else {
			interp_error(wk, args_node, "error: %s", strerror(ctx->err_no));
		}
		return false;
	}

	return true;
}

static bool
handle_special_dependency(struct workspace *wk, uint32_t node, uint32_t name,
	enum requirement_type requirement,  uint32_t *obj, bool *handled)
{
	if (strcmp(wk_objstr(wk, name), "threads") == 0) {
		*handled = true;
		struct obj *dep = make_obj(wk, obj, obj_dependency);
		dep->dat.dep.name = get_obj(wk, name)->dat.str;
		dep->dat.dep.flags |= dep_flag_found;
	} else {
		*handled = false;
	}

	return true;
}

struct parse_cflags_iter_ctx {
	uint32_t include_directories;
};

static enum iteration_result
parse_cflags_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct parse_cflags_iter_ctx *ctx = _ctx;
	static const char *pre = "-I";
	const uint32_t pre_len = strlen(pre);
	uint32_t s;

	if (strncmp(pre, wk_objstr(wk, val_id), pre_len) == 0) {
		make_obj(wk, &s, obj_file)->dat.file = wk_str_push(wk, &wk_objstr(wk, val_id)[pre_len]);

		obj_array_push(wk, ctx->include_directories, s);
	}

	return ir_cont;
}

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

	bool handled;
	if (!handle_special_dependency(wk, an[0].node, an[0].val, requirement, obj, &handled)) {
		return false;
	} else if (handled) {
		return true;
	}

	struct run_cmd_ctx ctx = { 0 };

	if (!pkg_config(wk, &ctx, an[0].node, "--modversion", wk_objstr(wk, an[0].val))) {
		return false;
	}

	struct obj *dep = make_obj(wk, obj, obj_dependency);
	dep->dat.dep.name = an[0].val;
	dep->dat.dep.version = wk_str_push_stripped(wk, ctx.out);

	if (ctx.status != 0) {
		if (requirement == requirement_required) {
			interp_error(wk, an[0].node, "required dependency not found");
			return false;
		}

		LOG_I(log_interp, "dependency %s not found", wk_objstr(wk, dep->dat.dep.name));
		return true;
	}

	LOG_I(log_interp, "dependency %s found: %s", wk_objstr(wk, dep->dat.dep.name), wk_str(wk, dep->dat.dep.version));

	dep->dat.dep.flags |= dep_flag_found;
	dep->dat.dep.flags |= dep_flag_pkg_config;

	if (!pkg_config(wk, &ctx, args_node, "--libs", wk_objstr(wk, an[0].val))) {
		return false;
	} else if (ctx.status != 0) {
		interp_error(wk, an[0].node, "unexpected pkg-config error: %s", ctx.err);
		return false;
	}

	get_obj(wk, *obj)->dat.dep.link_with = wk_str_split(wk, ctx.out, " \t\n");

	if (!pkg_config(wk, &ctx, args_node, "--cflags", wk_objstr(wk, an[0].val))) {
		return false;
	} else if (ctx.status != 0) {
		interp_error(wk, an[0].node, "unexpected pkg-config error: %s", ctx.err);
		return false;
	}

	make_obj(wk, &get_obj(wk, *obj)->dat.dep.include_directories, obj_array);

	struct parse_cflags_iter_ctx parse_cflags_iter_ctx  = {
		.include_directories = get_obj(wk, *obj)->dat.dep.include_directories
	};

	if (!obj_array_foreach(wk, wk_str_split(wk, ctx.out, " \t\n"), &parse_cflags_iter_ctx, parse_cflags_iter)) {
		return false;
	}
	return true;
}

