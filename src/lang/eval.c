#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "external/bestline.h"
#include "lang/eval.h"
#include "lang/interpreter.h"
#include "lang/parser.h"
#include "log.h"
#include "options.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "wrap.h"

bool
eval_project(struct workspace *wk, const char *subproject_name, const char *cwd,
	const char *build_dir, uint32_t *proj_id)
{
	char src[PATH_MAX];

	if (!path_join(src, PATH_MAX, cwd, "meson.build")) {
		return false;
	}

	if (!fs_file_exists(src)) {
		LOG_E("project %s does not contain a meson.build", cwd);
		return false;
	}

	bool ret = false;
	uint32_t parent_project = wk->cur_project;

	make_project(wk, &wk->cur_project, subproject_name, cwd, build_dir);
	*proj_id = wk->cur_project;

	const char *parent_prefix = log_get_prefix();
	char log_prefix[256] = { 0 };
	if (wk->cur_project > 0) {
		const char *clr = log_clr() ? "\033[35m" : "",
			   *no_clr = log_clr() ? "\033[0m" : "";
		snprintf(log_prefix, 255, "[%s%s%s]",
			clr,
			subproject_name,
			no_clr);
		log_set_prefix(log_prefix);
	}
	if (subproject_name) {
		LOG_I("entering subproject '%s'", subproject_name);
	}

	if (!setup_project_options(wk, cwd)) {
		goto cleanup;
	}

	if (!wk->eval_project_file(wk, src)) {
		goto cleanup;
	}

	if (wk->cur_project == 0 && !check_invalid_subproject_option(wk)) {
		goto cleanup;
	}

	ret = true;
cleanup:
	wk->cur_project = parent_project;

	log_set_prefix(parent_prefix);
	return ret;
}

bool
eval(struct workspace *wk, struct source *src, enum eval_mode mode, obj *res)
{
	/* L("evaluating '%s'", src->label); */
	interpreter_init();

	bool ret = false;
	struct ast ast = { 0 };

	struct source_data *sdata =
		darr_get(&wk->source_data, darr_push(&wk->source_data, &(struct source_data) { 0 }));

	enum parse_mode parse_mode = 0;
	if (mode == eval_mode_repl) {
		parse_mode |= pm_ignore_statement_with_no_effect;
	}

	if (!parser_parse(&ast, sdata, src, parse_mode)) {
		goto ret;
	}

	struct source *old_src = wk->src;
	struct ast *old_ast = wk->ast;

	wk->src = src;
	wk->ast = &ast;

	ret = wk->interp_node(wk, wk->ast->root, res);

	if (wk->subdir_done) {
		wk->subdir_done = false;
	}

	wk->src = old_src;
	wk->ast = old_ast;
ret:
	ast_destroy(&ast);
	return ret;
}

bool
eval_str(struct workspace *wk, const char *str, enum eval_mode mode, obj *res)
{
	struct source src = { .label = "<internal>", .src = str, .len = strlen(str) };
	return eval(wk, &src, mode, res);
}

bool
eval_project_file(struct workspace *wk, const char *path)
{
	/* L("evaluating '%s'", path); */
	bool ret = false;
	{
		obj_array_push(wk, wk->sources, make_str(wk, path));
	}

	struct source src = { 0 };
	if (!fs_read_entire_file(path, &src)) {
		return false;
	}

	obj res;
	if (!eval(wk, &src, eval_mode_default, &res)) {
		goto ret;
	}

	ret = true;
ret:
	fs_source_destroy(&src);
	return ret;
}

void
source_data_destroy(struct source_data *sdata)
{
	if (sdata->data) {
		z_free(sdata->data);
	}
}

void
repl(struct workspace *wk, bool dbg)
{
	bool loop = true;
	obj repl_res = 0;
	char *line;
	FILE *out = stderr;
	enum repl_cmd {
		repl_cmd_noop,
		repl_cmd_exit,
		repl_cmd_abort,
		repl_cmd_step,
		repl_cmd_list,
		repl_cmd_inspect,
		repl_cmd_help,
	};
	enum repl_cmd cmd = repl_cmd_noop;
	struct {
		const char *name[3];
		enum repl_cmd cmd;
		bool valid, has_arg;
		const char *help_text;
	} repl_cmds[] = {
		{ { "abort", 0 }, repl_cmd_abort, dbg },
		{ { "c", "continue", 0 }, repl_cmd_exit, dbg },
		{ { "exit", 0 }, repl_cmd_exit, !dbg },
		{ { "h", "help", 0 }, repl_cmd_help, true },
		{ { "i", "inspect", 0 }, repl_cmd_inspect, dbg, true, .help_text = "\\inspect <expr>" },
		{ { "l", "list", 0 }, repl_cmd_list, dbg },
		{ { "s", "step", 0 }, repl_cmd_step, dbg },
		0
	};

	if (dbg) {
		list_line_range(wk->src, get_node(wk->ast, wk->dbg.node)->line, 1);

		if (wk->dbg.stepping) {
			cmd = repl_cmd_step;
		}
	}

	const char *prompt = log_clr() ? "\033[36;1m>\033[0m " : "> ",
		   cmd_char = '\\';

	while (loop && (line = muon_bestline(prompt))) {
		muon_bestline_history_add(line);

		if (!*line || *line == cmd_char) {
			char *arg = NULL;

			if (!*line || !line[1]) {
				goto cmd_found;
			}

			if ((arg = strchr(line, ' '))) {
				*arg = 0;
				++arg;
			}

			uint32_t i, j;
			for (i = 0; *repl_cmds[i].name; ++i) {
				if (repl_cmds[i].valid) {
					for (j = 0; repl_cmds[i].name[j]; ++j) {
						if (strcmp(&line[1], repl_cmds[i].name[j]) == 0) {
							if (repl_cmds[i].has_arg) {
								if (!arg) {
									fprintf(out, "missing argument\n");
									goto cont;
								}
							} else {
								if (arg) {
									fprintf(out, "this command does not take an argument\n");
									goto cont;
								}
							}

							cmd = repl_cmds[i].cmd;
							goto cmd_found;
						}
					}
				}
			}

			fprintf(out, "unknown repl command '%s'\n", &line[1]);
			goto cont;
cmd_found:
			switch (cmd) {
			case repl_cmd_abort:
				exit(1);
				break;
			case repl_cmd_exit:
				wk->dbg.stepping = false;
				loop = false;
				break;
			case repl_cmd_help:
				fprintf(out, "repl commands:\n");
				for (i = 0; *repl_cmds[i].name; ++i) {
					if (!repl_cmds[i].valid) {
						continue;
					}

					fprintf(out, "  - ");
					for (j = 0; repl_cmds[i].name[j]; ++j) {
						fprintf(out, "%c%s", cmd_char, repl_cmds[i].name[j]);
						if (repl_cmds[i].name[j + 1]) {
							fprintf(out, ", ");
						}
					}

					if (repl_cmds[i].help_text) {
						fprintf(out, " - %s", repl_cmds[i].help_text);
					}
					fprintf(out, "\n");
				}
				break;
			case repl_cmd_list: {
				list_line_range(wk->src, get_node(wk->ast, wk->dbg.node)->line, 11);
				break;
			}
			case repl_cmd_step:
				wk->dbg.stepping = true;
				loop = false;
				break;
			case repl_cmd_inspect:
				if (!eval_str(wk, arg, eval_mode_repl, &repl_res)) {
					break;
				}

				obj_inspect(wk, out, repl_res);
				break;
			case repl_cmd_noop:
				break;
			}
		} else {
			cmd = repl_cmd_noop;

			if (!eval_str(wk, line, eval_mode_repl, &repl_res)) {
				goto cont;
			}

			if (repl_res) {
				obj_fprintf(wk, out, "%o\n", repl_res);
				hash_set_str(&wk->scope, "_", repl_res);
			}
		}
cont:
		muon_bestline_free(line);
	}

	muon_bestline_history_free();

	if (!line) {
		wk->dbg.stepping = false;
	}
}
