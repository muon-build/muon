/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "cmd_subprojects.h"
#include "lang/analyze.h"
#include "opts.h"
#include "platform/os.h"
#include "platform/path.h"
#include "wrap.h"

static obj
cmd_subprojects_escape_str(struct workspace *wk, const char *s)
{
	TSTR(escaped);
	tstr_push(wk, &escaped, '\'');
	str_escape(wk, &escaped, &STRL(s), false);
	tstr_push(wk, &escaped, '\'');
	return tstr_into_str(wk, &escaped);
}

static bool
cmd_subprojects_eval_cmd(struct workspace *wk,
	uint32_t argc,
	uint32_t argi,
	char *const argv[],
	const char *cmd,
	obj extra_args)
{
	obj cmd_args = make_obj(wk, obj_array);

	if (argc && argc > argi) {
		obj list = make_obj(wk, obj_array);
		for (; argc > argi; ++argi) {
			obj_array_push(wk, list, make_str(wk, argv[argi]));
		}

		TSTR(list_str);
		obj_to_s(wk, list, &list_str);
		obj_array_push(wk, cmd_args, tstr_into_str(wk, &list_str));
	}

	if (extra_args) {
		obj_array_extend(wk, cmd_args, extra_args);
	}

	obj joined;
	obj_array_join(wk, false, cmd_args, make_str(wk, ", "), &joined);

	char snippet[512];
	snprintf(snippet, sizeof(snippet), "import('subprojects').%s(%s)", cmd, get_str(wk, joined)->s);

	L("evaluating %s", snippet);

	obj res;
	return eval_str(wk, snippet, eval_mode_repl, &res);
}

static bool
cmd_subprojects_update(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace *wk = _ctx;

	OPTSTART("") {
	}
	OPTEND(argv[argi], " <list of subprojects>", "", NULL, -1)

	return cmd_subprojects_eval_cmd(wk, argc, argi, argv, "update", 0);
}

static bool
cmd_subprojects_list(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace *wk = _ctx;

	OPTSTART("") {
	}
	OPTEND(argv[argi], " <list of subprojects>", "", NULL, -1)

	obj extra_args = make_obj(wk, obj_array);
	obj_array_push(wk, extra_args, make_str(wk, "print: true"));

	return cmd_subprojects_eval_cmd(wk, argc, argi, argv, "list", extra_args);
}

static bool
cmd_subprojects_clean(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace *wk = _ctx;
	bool force = false;

	OPTSTART("f") {
	case 'f': force = true; break;
	}
	OPTEND(argv[argi], " <list of subprojects>", "  -f - force the operation\n", NULL, -1)

	wk->vm.behavior.assign_variable(wk, "force", make_obj_bool(wk, force), 0, assign_local);

	obj extra_args = make_obj(wk, obj_array);
	obj_array_push(wk, extra_args, make_str(wk, "force: force"));

	return cmd_subprojects_eval_cmd(wk, argc, argi, argv, "clean", extra_args);
}

static bool
cmd_subprojects_fetch(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace *wk = _ctx;
	bool force = false;
	const char *subprojects = 0;

	OPTSTART("fo:") {
	case 'f': force = true; break;
	case 'o': subprojects = optarg; break;
	}
	OPTEND(argv[argi], " <subproject.wrap>", "  -f - force the operation\n", NULL, 1)

	wk->vm.behavior.assign_variable(wk, "force", make_obj_bool(wk, force), 0, assign_local);

	obj extra_args = make_obj(wk, obj_array);

	{
		obj_array_push(wk, extra_args, cmd_subprojects_escape_str(wk, argv[argi]));

		if (subprojects) {
			obj_array_push(wk, extra_args, cmd_subprojects_escape_str(wk, subprojects));
		}
	}

	obj_array_push(wk, extra_args, make_str(wk, "force: force"));

	return cmd_subprojects_eval_cmd(wk, 0, 0, 0, "fetch", extra_args);
}

struct cmd_subprojects_ctx {
	struct workspace *wk;
};

bool
cmd_subprojects(void *_ctx, uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "update", cmd_subprojects_update, "update subprojects with .wrap files" },
		{ "list", cmd_subprojects_list, "list subprojects with .wrap files and their status" },
		{ "clean", cmd_subprojects_clean, "clean wrap-git subprojects" },
		{ "fetch", cmd_subprojects_fetch, "fetch a single subproject from a .wrap file" },
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

	TSTR(path);

	if (opts.dir) {
		tstr_pushs(&wk, &path, opts.dir);
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
		proj->subprojects_dir = tstr_into_str(&wk, &path);
	}

	wk.vm.lang_mode = language_extended;

	bool res = commands[cmd_i].cmd(&wk, argc, argi, argv);

	workspace_destroy(&wk);

	return res;
}
