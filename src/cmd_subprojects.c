/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>

#include "cmd_subprojects.h"
#include "lang/analyze.h"
#include "opts.h"
#include "platform/path.h"
#include "wrap.h"

static bool
cmd_subprojects_check_wrap(void *ctx, uint32_t argc, uint32_t argi, char *const argv[])
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

static void
cmd_subprojects_args_to_list(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	obj res = 0;
	make_obj(wk, &res, obj_array);

	if (argc > argi) {
		for (; argc > argi; ++argi) {
			obj_array_push(wk, res, make_str(wk, argv[argi]));
		}
	}

	wk->vm.behavior.assign_variable(wk, "argv", res, 0, assign_local);
}

static bool
cmd_subprojects_update(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace *wk = _ctx;

	OPTSTART("") {
	}
	OPTEND(argv[argi], " <list of subprojects>", "", NULL, -1)

	cmd_subprojects_args_to_list(wk, argc, argi, argv);

	obj res;
	return eval_str(wk, "import('subprojects').update(argv)", eval_mode_repl, &res);
}

static bool
cmd_subprojects_list(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace *wk = _ctx;

	OPTSTART("") {
	}
	OPTEND(argv[argi], " <list of subprojects>", "", NULL, -1)

	cmd_subprojects_args_to_list(wk, argc, argi, argv);

	obj res;
	return eval_str(wk, "import('subprojects').list(argv, print: true)", eval_mode_repl, &res);
}

static bool
cmd_subprojects_clean(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace *wk = _ctx;
	bool force;

	OPTSTART("f") {
	case 'f': force = true; break;
	}
	OPTEND(argv[argi], " <list of subprojects>", "  -f - force the operation", NULL, -1)

	cmd_subprojects_args_to_list(wk, argc, argi, argv);

	wk->vm.behavior.assign_variable(wk, "force", make_obj_bool(wk, force), 0, assign_local);

	obj res;
	return eval_str(wk, "import('subprojects').clean(argv, force: force)", eval_mode_repl, &res);
}

struct cmd_subprojects_ctx {
	struct workspace *wk;
};

bool
cmd_subprojects(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
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

	uint32_t cmd_i;
	if (!find_cmd(commands, &cmd_i, argc, argi, argv, false)) {
		return false;
	}

	struct workspace wk;
	workspace_init_bare(&wk);
	workspace_init_runtime(&wk);

	SBUF(path);

	if (opts.dir) {
		sbuf_pushs(&wk, &path, opts.dir);
	} else {
		struct workspace az_wk = { 0 };
		analyze_project_call(&az_wk);
		path_make_absolute(&wk, &path, get_cstr(&az_wk, current_project(&az_wk)->subprojects_dir));
		workspace_destroy(&az_wk);
	}

	{
		uint32_t id;
		make_project(&wk, &id, 0, wk.source_root, wk.build_root);
		struct project *proj = arr_get(&wk.projects, 0);
		proj->subprojects_dir = sbuf_into_str(&wk, &path);
	}

	wk.vm.lang_mode = language_extended;

	bool res = commands[cmd_i].cmd(&wk, argc, argi, argv);

	workspace_destroy(&wk);

	return res;
}
