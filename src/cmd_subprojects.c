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
cmd_subprojects_update(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	bool required = false;

	opt_for(-1, .usage_post = " <list of subprojects>") {
		if (opt_match('f', "fail if any subproject fails to update")) {
			required = true;
		}
	}
	opt_end();

	obj extra_args = 0;
	if (required) {
		extra_args = make_obj(wk, obj_array);
		obj_array_push(wk, extra_args, make_str(wk, "required: true"));
	}

	return cmd_subprojects_eval_cmd(wk, argc, argi, argv, "update", extra_args);
}

static bool
cmd_subprojects_list(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	opt_for(-1, .usage_post = " <list of subprojects>") {
	}
	opt_end();

	obj extra_args = make_obj(wk, obj_array);
	obj_array_push(wk, extra_args, make_str(wk, "print: true"));

	return cmd_subprojects_eval_cmd(wk, argc, argi, argv, "list", extra_args);
}

static bool
cmd_subprojects_clean(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	bool force = false;

	opt_for(-1, .usage_post = " <list of subprojects>") {
		if (opt_match('f', "actually perform the removal")) {
			force = true;
		}
	}
	opt_end();

	wk->vm.behavior.assign_variable(wk, "force", make_obj_bool(wk, force), 0, assign_local);

	obj extra_args = make_obj(wk, obj_array);
	obj_array_push(wk, extra_args, make_str(wk, "force: force"));

	return cmd_subprojects_eval_cmd(wk, argc, argi, argv, "clean", extra_args);
}

static bool
cmd_subprojects_fetch(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	const char *subprojects = 0;

	opt_for(-1, .usage_post = " <subproject.wrap>") {
		if (opt_match('o', "the directory to fetch into, e.g. subprojects", "directory")) {
			subprojects = opt_ctx.optarg;
		}
	}
	opt_end();

	obj extra_args = make_obj(wk, obj_array);

	{
		obj_array_push(wk, extra_args, cmd_subprojects_escape_str(wk, argv[argi]));

		if (subprojects) {
			obj_array_push(wk, extra_args, cmd_subprojects_escape_str(wk, subprojects));
		}
	}

	return cmd_subprojects_eval_cmd(wk, 0, 0, 0, "fetch", extra_args);
}

struct cmd_subprojects_ctx {
	struct workspace *wk;
};

bool
cmd_subprojects(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	static struct opt_command commands[] = {
		{ "update", cmd_subprojects_update, "update subprojects with .wrap files" },
		{ "list", cmd_subprojects_list, "list subprojects with .wrap files and their status" },
		{ "clean", cmd_subprojects_clean, "clean wrap-git subprojects" },
		{ "fetch", cmd_subprojects_fetch, "fetch a single subproject from a .wrap file" },
		{ 0 },
	};

	struct {
		const char *dir;
	} opts = { 0 };

	opt_for(-1, commands) {
		if (opt_match('d', "manually specify subprojects directory", "directory")) {
			opts.dir = opt_ctx.optarg;
		}
	}
	opt_end();

	uint32_t cmd_i;
	if (!opt_find_cmd(commands, &cmd_i, argc, argi, argv, false)) {
		return false;
	}

	workspace_init_runtime(wk);

	TSTR(path);

	if (opts.dir) {
		tstr_pushs(wk, &path, opts.dir);
	} else {
		workspace_perm_begin(wk);

		struct workspace az_wk = { 0 };
		workspace_init_bare(&az_wk, wk->a, wk->a_scratch);
		analyze_project_call(&az_wk);
		path_make_absolute(wk, &path, get_cstr(&az_wk, current_project(&az_wk)->subprojects_dir));

		workspace_perm_end(wk);
	}

	{
		// TODO: could be make_dummy_project?
		uint32_t id;
		make_project(wk, &id, 0, wk->source_root, wk->build_root);
		struct project *proj = arr_get(&wk->projects, 0);
		proj->subprojects_dir = tstr_into_str(wk, &path);
	}

	wk->vm.lang_mode = language_extended;

	bool res = commands[cmd_i].cmd(wk, argc, argi, argv);

	return res;
}
