/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>

#include "cmd_subprojects.h"
#include "log.h"
#include "opts.h"
#include "platform/path.h"
#include "wrap.h"

static const char *cmd_subprojects_subprojects_dir = "subprojects";

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

struct cmd_subprojects_download_ctx {
	const char *subprojects;
};

static enum iteration_result
cmd_subprojects_download_iter(void *_ctx, const char *name)
{
	struct cmd_subprojects_download_ctx *ctx = _ctx;
	uint32_t len = strlen(name);
	SBUF_manual(path);

	if (len <= 5 || strcmp(&name[len - 5], ".wrap") != 0) {
		goto cont;
	}

	path_join(NULL, &path, ctx->subprojects, name);

	if (!fs_file_exists(path.buf)) {
		goto cont;
	}

	LOG_I("fetching %s", name);
	struct wrap wrap = { 0 };
	if (!wrap_handle(path.buf, ctx->subprojects, &wrap, true)) {
		goto cont;
	}

	wrap_destroy(&wrap);
cont:
	sbuf_destroy(&path);
	return ir_cont;
}

static bool
cmd_subprojects_download(uint32_t argc, uint32_t argi, char *const argv[])
{
	bool res = false;

	OPTSTART("") {
	}
	OPTEND(argv[argi], " <list of subprojects>", "", NULL, -1)

	SBUF_manual(path);
	path_make_absolute(NULL, &path, cmd_subprojects_subprojects_dir);

	struct cmd_subprojects_download_ctx ctx = {
		.subprojects = path.buf,
	};

	if (argc > argi) {
		SBUF_manual(wrap_file);

		for (; argc > argi; ++argi) {
			path_join(NULL, &wrap_file, ctx.subprojects, argv[argi]);

			sbuf_pushs(NULL, &wrap_file, ".wrap");

			if (!fs_file_exists(wrap_file.buf)) {
				LOG_E("wrap file for '%s' not found", argv[argi]);
				goto ret;
			}

			if (cmd_subprojects_download_iter(&ctx, wrap_file.buf) == ir_err) {
				goto ret;
			}
		}

		sbuf_destroy(&wrap_file);
		res = true;
	} else {
		res = fs_dir_foreach(path.buf, &ctx, cmd_subprojects_download_iter);
	}

ret:
	sbuf_destroy(&path);
	return res;
}

bool
cmd_subprojects(uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "check-wrap", cmd_subprojects_check_wrap, "check if a wrap file is valid" },
		{ "download", cmd_subprojects_download, "download subprojects" },
		{ 0 },
	};

	OPTSTART("d:") {
	case 'd': cmd_subprojects_subprojects_dir = optarg; break;
	}
	OPTEND(argv[0], "", "  -d <directory> - use an alternative subprojects directory\n", commands, -1)

	cmd_func cmd;
	if (!find_cmd(commands, &cmd, argc, argi, argv, false)) {
		return false;
	}

	return cmd(argc, argi, argv);
}
