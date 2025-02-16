/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "functions/modules/subprojects.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/path.h"
#include "wrap.h"

struct subprojects_foreach_ctx {
	subprojects_foreach_cb cb;
	struct subprojects_common_ctx *usr_ctx;
	struct workspace *wk;
	const char *subprojects_dir;
};

static const char *
subprojects_dir(struct workspace *wk)
{
	return get_cstr(wk, current_project(wk)->subprojects_dir);
}

static enum iteration_result
subprojects_foreach_iter(void *_ctx, const char *name)
{
	struct subprojects_foreach_ctx *ctx = _ctx;
	uint32_t len = strlen(name);
	TSTR_manual(path);

	if (len <= 5 || strcmp(&name[len - 5], ".wrap") != 0) {
		return ir_cont;
	}

	path_join(ctx->wk, &path, subprojects_dir(ctx->wk), name);

	if (!fs_file_exists(path.buf)) {
		return ir_cont;
	}

	return ctx->cb(ctx->wk, ctx->usr_ctx, path.buf);
}

bool
subprojects_foreach(struct workspace *wk, obj list, struct subprojects_common_ctx *usr_ctx, subprojects_foreach_cb cb)
{
	if (list && get_obj_array(wk, list)->len) {
		bool res = true;
		TSTR_manual(wrap_file);

		obj v;
		obj_array_for(wk, list, v) {
			const char *name = get_cstr(wk, v);
			path_join(wk, &wrap_file, subprojects_dir(wk), name);

			tstr_pushs(wk, &wrap_file, ".wrap");

			if (!fs_file_exists(wrap_file.buf)) {
				LOG_E("wrap file for '%s' not found", name);
				res = false;
				break;
			}

			if (cb(wk, usr_ctx, wrap_file.buf) == ir_err) {
				res = false;
				break;
			}
		}

		return res;
	} else if (fs_dir_exists(subprojects_dir(wk))) {
		struct subprojects_foreach_ctx ctx = {
			.cb = cb,
			.usr_ctx = usr_ctx,
			.wk = wk,
		};

		return fs_dir_foreach(subprojects_dir(wk), &ctx, subprojects_foreach_iter);
	}

	return true;
}

static enum iteration_result
func_subprojects_update_iter(struct workspace *wk, struct subprojects_common_ctx *ctx, const char *path)
{
	struct wrap wrap = { 0 };
	struct wrap_opts wrap_opts = {
		.allow_download = true,
		.subprojects = subprojects_dir(wk),
		.mode = wrap_handle_mode_update,
	};
	bool ok = wrap_handle(path, &wrap, &wrap_opts);

	obj_array_push(wk, *ctx->res, make_str(wk, wrap.name.buf));

	if (!ok) {
		++ctx->failed;
		goto cont;
	}
	wrap_destroy(&wrap);
cont:
	return ir_cont;
}

static bool
func_subprojects_update(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ TYPE_TAG_LISTIFY | tc_string, .optional = true, .desc = "A list of subprojects to operate on." },
		ARG_TYPE_NULL,
	};

	if (!pop_args(wk, an, 0)) {
		return false;
	}

	make_obj(wk, res, obj_array);
	struct subprojects_common_ctx ctx = {
		.print = true,
		.res = res,
	};

	subprojects_foreach(wk, an[0].val, &ctx, func_subprojects_update_iter);

	return ctx.failed == 0;
}

static enum iteration_result
subprojects_list_iter(struct workspace *wk, struct subprojects_common_ctx *ctx, const char *path)
{
	struct wrap wrap = { 0 };
	struct wrap_opts wrap_opts = {
		.allow_download = false,
		.subprojects = subprojects_dir(wk),
		.mode = wrap_handle_mode_check_dirty,
	};
	if (!wrap_handle(path, &wrap, &wrap_opts)) {
		goto cont;
	}

	char *t = "file";
	if (wrap.type == wrap_type_git) {
		t = "git ";
	}

	obj d;
	make_obj(wk, &d, obj_dict);
	obj_dict_set(wk, d, make_str(wk, "name"), make_str(wk, wrap.name.buf));
	obj_dict_set(wk, d, make_str(wk, "type"), make_str(wk, t));
	obj_dict_set(wk, d, make_str(wk, "outdated"), make_obj_bool(wk, wrap.outdated));
	obj_dict_set(wk, d, make_str(wk, "dirty"), make_obj_bool(wk, wrap.dirty));
	obj_array_push(wk, *ctx->res, d);

	if (ctx->print) {
		const char *clr_green = log_clr() ? "\033[32m" : "", *clr_blue = log_clr() ? "\033[34m" : "",
			   *clr_magenta = log_clr() ? "\033[35m" : "", *clr_off = log_clr() ? "\033[0m" : "";

		const char *t_clr = clr_blue;
		if (wrap.type == wrap_type_git) {
			t_clr = clr_magenta;
		}

		LLOG_I("[%s%s%s] %s ", t_clr, t, clr_off, wrap.name.buf);

		if (wrap.outdated) {
			log_plain("%sU%s", clr_green, clr_off);
		}
		if (wrap.dirty) {
			log_plain("*");
		}

		log_plain("\n");
	}

	wrap_destroy(&wrap);

cont:
	return ir_cont;
}

static bool
func_subprojects_list(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ TYPE_TAG_LISTIFY | tc_string, .optional = true, .desc = "A list of subprojects to operate on." },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_print,
	};
	struct args_kw akw[] = {
		[kw_print] = { "print", tc_bool, .desc = "Print out a formatted list of subprojects as well as returning it." },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	make_obj(wk, res, obj_array);
	struct subprojects_common_ctx ctx = {
		.print = get_obj_bool_with_default(wk, akw[kw_print].val, false),
		.res = res,
	};

	return subprojects_foreach(wk, an[0].val, &ctx, subprojects_list_iter);
}

static enum iteration_result
subprojects_clean_iter(struct workspace *wk, struct subprojects_common_ctx *ctx, const char *path)
{
	struct wrap wrap = { 0 };
	if (!wrap_parse(path, &wrap)) {
		goto cont;
	}

	if (wrap.type != wrap_type_git) {
		goto cont;
	}

	if (!fs_dir_exists(wrap.dest_dir.buf)) {
		goto cont;
	}
	if (ctx->force) {
		LOG_I("removing %s", wrap.dest_dir.buf);
		fs_rmdir_recursive(wrap.dest_dir.buf, true);
		fs_rmdir(wrap.dest_dir.buf, true);

		obj_array_push(wk, *ctx->res, make_str(wk, wrap.name.buf));
	} else {
		LOG_I("would remove %s", wrap.dest_dir.buf);
	}

	wrap_destroy(&wrap);

cont:
	return ir_cont;
}

static bool
func_subprojects_clean(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ TYPE_TAG_LISTIFY | tc_string, .optional = true, .desc = "A list of subprojects to operate on." },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_force,
	};
	struct args_kw akw[] = {
		[kw_force] = { "force", tc_bool, .desc = "Force the operation." },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	make_obj(wk, res, obj_array);
	struct subprojects_common_ctx ctx = {
		.force = get_obj_bool_with_default(wk, akw[kw_force].val, false),
		.print = true,
		.res = res,
	};

	return subprojects_foreach(wk, an[0].val, &ctx, subprojects_clean_iter);
}

const struct func_impl impl_tbl_module_subprojects[] = {
	{ "update", func_subprojects_update, .fuzz_unsafe = true, .desc = "Update subprojects with .wrap files" },
	{ "list",
		func_subprojects_list,
		tc_array,
		.fuzz_unsafe = true,
		.desc = "List subprojects with .wrap files and their status." },
	{ "clean", func_subprojects_clean, .fuzz_unsafe = true, .desc = "Clean wrap-git subprojects" },
	{ NULL, NULL },
};
