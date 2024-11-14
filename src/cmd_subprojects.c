/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>

#include "cmd_subprojects.h"
#include "lang/analyze.h"
#include "log.h"
#include "opts.h"
#include "platform/path.h"
#include "wrap.h"

static const char *cmd_subprojects_subprojects_dir;

typedef enum iteration_result (*cmd_subprojects_foreach_cb)(void *_ctx, const char *name);

struct cmd_subprojects_foreach_ctx {
	cmd_subprojects_foreach_cb cb;
	void *usr_ctx;
};

static enum iteration_result
cmd_subprojects_foreach_iter(void *_ctx, const char *name)
{
	struct cmd_subprojects_foreach_ctx *ctx = _ctx;
	uint32_t len = strlen(name);
	SBUF_manual(path);
	enum iteration_result res = ir_cont;

	if (len <= 5 || strcmp(&name[len - 5], ".wrap") != 0) {
		goto cont;
	}

	path_join(NULL, &path, cmd_subprojects_subprojects_dir, name);

	if (!fs_file_exists(path.buf)) {
		goto cont;
	}

	res = ctx->cb(ctx->usr_ctx, path.buf);

cont:
	sbuf_destroy(&path);
	return res;
}

static bool
cmd_subprojects_foreach(uint32_t argc, uint32_t argi, char *const argv[], void *usr_ctx, cmd_subprojects_foreach_cb cb)
{
	if (argc > argi) {
		bool res = true;
		SBUF_manual(wrap_file);

		for (; argc > argi; ++argi) {
			path_join(NULL, &wrap_file, cmd_subprojects_subprojects_dir, argv[argi]);

			sbuf_pushs(NULL, &wrap_file, ".wrap");

			if (!fs_file_exists(wrap_file.buf)) {
				LOG_E("wrap file for '%s' not found", argv[argi]);
				res = false;
				break;
			}

			if (cb(usr_ctx, wrap_file.buf) == ir_err) {
				res = false;
				break;
			}
		}

		sbuf_destroy(&wrap_file);
		return res;
	} else {
		struct cmd_subprojects_foreach_ctx ctx = {
			.cb = cb,
			.usr_ctx = usr_ctx,
		};

		return fs_dir_foreach(cmd_subprojects_subprojects_dir, &ctx, cmd_subprojects_foreach_iter);
	}
}

static bool
cmd_subprojects_check_wrap(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *filename;
	} opts = { 0 };

	OPTSTART("") {
	}
	OPTEND(argv[argi], " <filename>", "", NULL, 1)

	opts.filename = argv[argi];

	bool ret = false;

	struct wrap wrap = { 0 };
	if (!wrap_parse(opts.filename, &wrap)) {
		goto ret;
	}

	ret = true;
ret:
	wrap_destroy(&wrap);
	return ret;
}

static enum iteration_result
cmd_subprojects_update_iter(void *_ctx, const char *path)
{
	struct wrap wrap = { 0 };
	struct wrap_opts wrap_opts = {
		.allow_download = true,
		.subprojects = cmd_subprojects_subprojects_dir,
		.mode = wrap_handle_mode_update,
	};
	if (!wrap_handle(path, &wrap, &wrap_opts)) {
		goto cont;
	}
	wrap_destroy(&wrap);
cont:
	return ir_cont;
}

static bool
cmd_subprojects_update(uint32_t argc, uint32_t argi, char *const argv[])
{
	OPTSTART("") {
	}
	OPTEND(argv[argi], " <list of subprojects>", "", NULL, -1)

	return cmd_subprojects_foreach(argc, argi, argv, 0, cmd_subprojects_update_iter);
}

static enum iteration_result
cmd_subprojects_list_iter(void *_ctx, const char *path)
{
	struct wrap wrap = { 0 };
	struct wrap_opts wrap_opts = {
		.allow_download = false,
		.subprojects = cmd_subprojects_subprojects_dir,
		.mode = wrap_handle_mode_check_dirty,
	};
	if (!wrap_handle(path, &wrap, &wrap_opts)) {
		goto cont;
	}

	char *clr_green = log_clr() ? "\033[32m" : "",
		 *clr_blue = log_clr() ? "\033[34m" : "",
		 *clr_magenta = log_clr() ? "\033[35m" : "",
	     *clr_off = log_clr() ? "\033[0m" : "";

	char *t = "file", *t_clr = clr_blue;
	if (wrap.type == wrap_type_git) {
		t = "git ";
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

	wrap_destroy(&wrap);

cont:
	return ir_cont;
}

static bool
cmd_subprojects_list(uint32_t argc, uint32_t argi, char *const argv[])
{
	OPTSTART("") {
	}
	OPTEND(argv[argi], " <list of subprojects>", "", NULL, -1)

	return cmd_subprojects_foreach(argc, argi, argv, 0, cmd_subprojects_list_iter);
}

struct cmd_subprojects_clean_ctx {
	bool force;
};

static enum iteration_result
cmd_subprojects_clean_iter(void *_ctx, const char *path)
{
	struct cmd_subprojects_clean_ctx *ctx = _ctx;
	struct wrap wrap = { 0 };
	if (!wrap_parse(path, &wrap)) {
		goto cont;
	}

	if (wrap.type != wrap_type_git) {
		goto cont;
	}

	if (ctx->force) {
		LOG_I("removing %s", wrap.dest_dir.buf);
		fs_rmdir_recursive(wrap.dest_dir.buf, true);
		fs_rmdir(wrap.dest_dir.buf, true);
	} else {
		LOG_I("would remove %s", wrap.dest_dir.buf);
	}

	wrap_destroy(&wrap);

cont:
	return ir_cont;
}

static bool
cmd_subprojects_clean(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct cmd_subprojects_clean_ctx ctx = { 0 };

	OPTSTART("f") {
	case 'f': ctx.force = true; break;
	}
	OPTEND(argv[argi], " <list of subprojects>", "  -f - force the operation", NULL, -1)

	return cmd_subprojects_foreach(argc, argi, argv, &ctx, cmd_subprojects_clean_iter);
}

bool
cmd_subprojects(uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "check-wrap", cmd_subprojects_check_wrap, "check if a wrap file is valid" },
		{ "update", cmd_subprojects_update, "update subprojects with .wrap files" },
		{ "list", cmd_subprojects_list, "list subprojects with .wrap files and their status" },
		{ "clean", cmd_subprojects_clean, "clean wrap-git subprojects" },
		{ 0 },
	};

	struct {
		const char *dir;
	} opts = { 0 };

	OPTSTART("d:") {
	case 'd': opts.dir = optarg; break;
	}
	OPTEND(argv[0], "", "  -d <directory> - manually specify subprojects directory\n", commands, -1)

	cmd_func cmd;
	if (!find_cmd(commands, &cmd, argc, argi, argv, false)) {
		return false;
	}

	SBUF_manual(path);

	if (opts.dir) {
	} else {
		struct workspace wk = { 0 };
		analyze_project_call(&wk);
		path_make_absolute(0, &path, get_cstr(&wk, current_project(&wk)->subprojects_dir));
		workspace_destroy(&wk);
	}
	cmd_subprojects_subprojects_dir = path.buf;

	bool res = cmd(argc, argi, argv);

	sbuf_destroy(&path);
	return res;
}
