/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "external/readline.h"
#include "functions/modules.h"
#include "lang/compiler.h"
#include "lang/eval.h"
#include "lang/parser.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "tracy.h"
#include "wrap.h"

bool
eval_project(struct workspace *wk,
	const char *subproject_name,
	const char *cwd,
	const char *build_dir,
	uint32_t *proj_id)
{
	bool ret = false;
	uint32_t parent_project = wk->cur_project;

	make_project(wk, &wk->cur_project, subproject_name, cwd, build_dir);
	*proj_id = wk->cur_project;

	stack_push(&wk->stack, wk->vm.scope_stack, current_project(wk)->scope_stack);

	obj parent_eval_trace = wk->vm.dbg_state.eval_trace;

	if (wk->cur_project > 0) {
		log_set_prefix(2);
	}

	if (subproject_name && !wk->vm.in_analyzer) {
		L("entering subproject '%s'", subproject_name);
	}

	enum build_language lang;
	const char *build_file = determine_build_file(wk, cwd, &lang);

	if (!build_file) {
		goto cleanup;
	}

	if (!setup_project_options(wk, cwd)) {
		goto cleanup;
	}

	wk->vm.dbg_state.eval_trace_subdir = true;

	if (!wk->vm.behavior.eval_project_file(wk, build_file, lang, eval_project_file_flag_first, 0)) {
		goto cleanup;
	}

	if (wk->cur_project == 0 && !check_invalid_subproject_option(wk)) {
		goto cleanup;
	}

	ret = true;
cleanup:
	wk->vm.dbg_state.eval_trace = parent_eval_trace;
	if (wk->cur_project > 0) {
		L("leaving subproject '%s'", subproject_name);
		log_set_prefix(-2);
	}
	wk->cur_project = parent_project;
	stack_pop(&wk->stack, wk->vm.scope_stack);

	return ret;
}

static bool
ensure_project_is_first_statement(struct workspace *wk, const struct source *src, struct node *n, bool check_only)
{
	bool first_statement_is_a_call_to_project = n->type == node_type_stmt && n->l && n->l->type == node_type_call
						    && n->l->r && n->l->r->type == node_type_id_lit
						    && str_eql(get_str(wk, n->l->r->data.str), &STR("project"));

	if (!first_statement_is_a_call_to_project) {
		if (!check_only) {
			error_message(src, n->location, log_error, 0, "first statement is not a call to project()");
		}
		return false;
	}
	return true;
}

bool
eval(struct workspace *wk, const struct source *src, enum build_language lang, enum eval_mode mode, obj *res)
{
	TracyCZoneAutoS;
	TracyCMessage(src->label, strlen(src->label));

	obj res_ignored;
	if (!res) {
		res = &res_ignored;
	}

	if (lang == build_language_cmake && mode == eval_mode_first) {
		stack_push(&wk->stack, wk->vm.lang_mode, language_extended);
		obj _;
		bool ok = module_import(wk, "cmake_prelude", false, &_);
		stack_pop(&wk->stack, wk->vm.lang_mode);

		assert(ok);
	}

	arr_push(&wk->vm.src, src);
	src = arr_peek(&wk->vm.src, 1);

	enum vm_compile_mode compile_mode
		= (wk->vm.lang_mode == language_extended || wk->vm.lang_mode == language_internal) ?
			  vm_compile_mode_language_extended :
			  0;

	if (mode & eval_mode_repl) {
		compile_mode |= vm_compile_mode_expr;
	}
	if (mode & eval_mode_return_after_project) {
		compile_mode |= vm_compile_mode_return_after_project;
	}
	if (mode & eval_mode_relaxed_parse) {
		compile_mode |= vm_compile_mode_relaxed_parse;
	}

	uint32_t entry;
	{
		struct node *n = 0;

		vm_compile_state_reset(wk);

		switch (lang) {
		case build_language_meson: n = parse(wk, src, compile_mode); break;
		case build_language_cmake: n = cm_parse(wk, src); break;
		}

		if (!n) {
			TracyCZoneAutoE;
			return false;
		}

		bool first = lang == build_language_meson && (mode & eval_mode_first);
		if (first) {
			if (!ensure_project_is_first_statement(wk, src, n, false)) {
				TracyCZoneAutoE;
				return false;
			}
		}

		if (!vm_compile_ast(wk, n, compile_mode, &entry)) {
			TracyCZoneAutoE;
			return false;
		}
	}

	log_progress_push_level(entry, wk->vm.code.len);

	if (wk->vm.dbg_state.eval_trace) {
		obj_array_push(wk,
			wk->vm.dbg_state.eval_trace,
			make_strf(wk, "%s%s", src->type == source_type_embedded ? "[embedded] " : "", src->label));
		bool trace_subdir = wk->vm.dbg_state.eval_trace_subdir;
		if (trace_subdir) {
			obj subdir_eval_trace;
			subdir_eval_trace = make_obj(wk, obj_array);
			obj_array_push(wk, wk->vm.dbg_state.eval_trace, subdir_eval_trace);
			stack_push(&wk->stack, wk->vm.dbg_state.eval_trace, subdir_eval_trace);
		}
		stack_push(&wk->stack, wk->vm.dbg_state.eval_trace_subdir, false);
	}

	uint32_t call_stack_base = wk->vm.call_stack.len;
	struct call_frame eval_frame = {
		.type = call_frame_type_eval,
		.return_ip = wk->vm.ip,
	};
	vm_push_call_stack_frame(wk, &eval_frame);

	wk->vm.ip = entry;

	*res = vm_execute(wk);
	assert(call_stack_base == wk->vm.call_stack.len);

	if (wk->vm.dbg_state.eval_trace) {
		stack_pop(&wk->stack, wk->vm.dbg_state.eval_trace_subdir);
		if (wk->vm.dbg_state.eval_trace_subdir) {
			stack_pop(&wk->stack, wk->vm.dbg_state.eval_trace);
		}
	}

	log_progress_pop_level();

	bool ok = !wk->vm.error;
	wk->vm.error = false;

	TracyCZoneAutoE;
	return ok;
}

bool
eval_str_label(struct workspace *wk, const char *label, const char *str, enum eval_mode mode, obj *res)
{
	struct source src = { .label = get_cstr(wk, make_str(wk, label)), .src = str, .len = strlen(str) };
	return eval(wk, &src, build_language_meson, mode, res);
}

bool
eval_str(struct workspace *wk, const char *str, enum eval_mode mode, obj *res)
{
	return eval_str_label(wk, "<internal>", str, mode, res);
}

bool
eval_project_file(struct workspace *wk, const char *path, enum build_language lang, enum eval_project_file_flags flags, obj *res)
{
	bool ret = false;
	obj path_str = make_str(wk, path);
	workspace_add_regenerate_deps(wk, path_str);

	struct source src = { 0 };
	if (!fs_read_entire_file(get_str(wk, path_str)->s, &src)) {
		return false;
	}

	enum eval_mode eval_mode = 0;
	if (flags & eval_project_file_flag_first) {
		eval_mode |= eval_mode_first;
	}
	if (flags & eval_project_file_flag_return_after_project) {
		eval_mode |= eval_mode_return_after_project;
	}
	if (flags & eval_project_file_flag_relaxed_parse) {
		eval_mode |= eval_mode_relaxed_parse;
	}

	if (!eval(wk, &src, lang, eval_mode, res)) {
		goto ret;
	}

	ret = true;
ret:
	return ret;
}

static bool
repl_eval_str(struct workspace *wk, const char *str, obj *repl_res)
{
	stack_push(&wk->stack, wk->vm.dbg_state.stepping, false);
	bool ret = eval_str(wk, str, eval_mode_repl, repl_res);
	stack_pop(&wk->stack, wk->vm.dbg_state.stepping);
	return ret;
}

void
repl(struct workspace *wk, bool dbg)
{
	bool loop = true;
	obj repl_res = 0;
	char *line;
	FILE *out = _log_file();
	enum repl_cmd {
		repl_cmd_noop,
		repl_cmd_exit,
		repl_cmd_abort,
		repl_cmd_step,
		repl_cmd_list,
		repl_cmd_inspect,
		repl_cmd_watch,
		repl_cmd_unwatch,
		repl_cmd_eval,
		repl_cmd_breakpoint,
		repl_cmd_backtrace,
		repl_cmd_help,
	};
	static enum repl_cmd cmd = repl_cmd_noop;
	struct {
		const char *name[5];
		enum repl_cmd cmd;
		bool valid, has_arg;
		const char *help_text;
	} repl_cmds[] = { { { "abort", 0 }, repl_cmd_abort, dbg },
		{ { "c", "continue", 0 }, repl_cmd_exit, dbg },
		{ { "exit", 0 }, repl_cmd_exit, !dbg },
		{ { "h", "help", 0 }, repl_cmd_help, true },
		{ { "i", "inspect", 0 }, repl_cmd_inspect, true, true },
		{ { "l", "list", 0 }, repl_cmd_list, dbg },
		{ { "s", "step", 0 }, repl_cmd_step, dbg },
		{ { "w", "watch", 0 }, repl_cmd_watch, dbg, true },
		{ { "uw", "unwatch", 0 }, repl_cmd_unwatch, dbg, true },
		{ { "e", "p", "eval", "print", 0 }, repl_cmd_eval, true, true },
		{ { "br", "breakpoint", 0 }, repl_cmd_breakpoint, dbg, true },
		{ { "bt", "backtrace", 0 }, repl_cmd_backtrace, dbg },
		0 };

	if (dbg) {
		struct source_location loc;
		uint32_t src_idx;
		vm_lookup_inst_location_src_idx(&wk->vm, wk->vm.ip, &loc, &src_idx);
		list_line_range(arr_get(&wk->vm.src, src_idx), loc, 1);
	}

	const char *prompt = "> ";
	char *arg = NULL;

	while (loop && (line = muon_readline(prompt))) {
		if (!*line) {
			goto cmd_found;
		}

		muon_readline_history_add(line);

		if ((arg = strchr(line, ' '))) {
			*arg = 0;
			++arg;
		}

		uint32_t i, j;
		for (i = 0; *repl_cmds[i].name; ++i) {
			if (repl_cmds[i].valid) {
				for (j = 0; repl_cmds[i].name[j]; ++j) {
					if (strcmp(line, repl_cmds[i].name[j]) == 0) {
						if (repl_cmds[i].has_arg) {
							if (!arg) {
								fprintf(out, "missing argument\n");
								continue;
							}
						} else {
							if (arg) {
								fprintf(out,
									"this command does not take an argument\n");
								continue;
							}
						}

						cmd = repl_cmds[i].cmd;
						goto cmd_found;
					}
				}
			}
		}

		fprintf(out, "unknown repl command '%s'\n", line);
		continue;

cmd_found:
		switch (cmd) {
		case repl_cmd_abort: exit(1); break;
		case repl_cmd_exit: {
			wk->vm.dbg_state.stepping = false;
			loop = false;
			break;
		}
		case repl_cmd_help:
			fprintf(out, "repl commands:\n");
			for (i = 0; *repl_cmds[i].name; ++i) {
				if (!repl_cmds[i].valid) {
					continue;
				}

				fprintf(out, "  - ");
				for (j = 0; repl_cmds[i].name[j]; ++j) {
					fprintf(out, "%s", repl_cmds[i].name[j]);
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
			struct source_location loc;
			uint32_t src_idx;
			vm_lookup_inst_location_src_idx(&wk->vm, wk->vm.ip, &loc, &src_idx);
			list_line_range(arr_get(&wk->vm.src, src_idx), loc, 11);
			break;
		}
		case repl_cmd_step: {
			wk->vm.dbg_state.stepping = true;
			loop = false;
			break;
		}
		case repl_cmd_inspect:
			if (!repl_eval_str(wk, arg, &repl_res)) {
				break;
			}

			obj_inspect(wk, repl_res);
			break;
		case repl_cmd_watch:
			if (!wk->vm.dbg_state.watched) {
				wk->vm.dbg_state.watched = make_obj(wk, obj_array);
			}

			obj_array_push(wk, wk->vm.dbg_state.watched, make_str(wk, arg));
			break;
		case repl_cmd_unwatch:
			if (wk->vm.dbg_state.watched) {
				uint32_t idx;
				if (obj_array_index_of(wk, wk->vm.dbg_state.watched, make_str(wk, arg), &idx)) {
					obj_array_del(wk, wk->vm.dbg_state.watched, idx);
				}
			}
			break;
		case repl_cmd_eval: {
			if (!repl_eval_str(wk, arg, &repl_res)) {
				continue;
			}

			if (repl_res) {
				obj_fprintf(wk, out, "%o\n", repl_res);
				wk->vm.behavior.assign_variable(wk, "_", repl_res, 0, assign_local);
			}
			break;
		}
		case repl_cmd_breakpoint: {
			vm_dbg_push_breakpoint_str(wk, arg);
			break;
		}
		case repl_cmd_backtrace: {
			uint32_t i;
			struct call_frame frame;
			struct source *src;
			struct source_location loc;
			for (i = 1; i < wk->vm.call_stack.len + 1; ++i) {
				if (i == wk->vm.call_stack.len) {
					frame = (struct call_frame){
						.return_ip = wk->vm.ip - 1,
					};
				} else {
					frame = *(struct call_frame *)arr_get(&wk->vm.call_stack, i);
				}

				vm_lookup_inst_location(&wk->vm, frame.return_ip, &loc, &src);

				error_message(src, loc, log_info, 0, "");
			}
			break;
		}
		case repl_cmd_noop: break;
		}
	}

	muon_readline_history_free();
}

const char *
determine_project_root(struct workspace *wk, const char *path)
{
	TSTR(tmp);
	TSTR(new_path);

	path_make_absolute(wk, &new_path, path);
	path_basename(wk, &tmp, new_path.buf);
	if (strcmp(tmp.buf, "meson.build") != 0) {
		path_dirname(wk, &tmp, new_path.buf);
		path_join(wk, &new_path, tmp.buf, "meson.build");
	}
	path = new_path.buf;

	while (true) {
		if (!fs_file_exists(path)) {
			goto cont;
		}

		struct node *n;
		struct source src = { 0 };

		if (!fs_read_entire_file(path, &src)) {
			goto cont;
		} else if (!(n = parse(wk, &src, vm_compile_mode_quiet | vm_compile_mode_relaxed_parse))) {
			// If we failed to parse this file, try just searching for the string project(
			struct str src_str = { src.src, src.len };
			if (str_startswith(&src_str, &STR("project(")) || str_contains(&src_str, &STR("\nproject("))) {
				goto found;
			}
			goto cont;
		}

		if (ensure_project_is_first_statement(wk, 0, n, true)) {
found:
			// found
			path_dirname(wk, &tmp, path);
			obj s = tstr_into_str(wk, &tmp);
			fs_source_destroy(&src);
			return get_cstr(wk, s);
		}

cont:
		fs_source_destroy(&src);
		path_dirname(wk, &tmp, path);
		path_dirname(wk, &new_path, tmp.buf);
		if (strcmp(new_path.buf, tmp.buf) == 0) {
			return NULL;
		}

		path_push(wk, &new_path, "meson.build");
		path = new_path.buf;
	}
}

const char *
determine_build_file(struct workspace *wk, const char *cwd, enum build_language *out_lang)
{
	const struct {
		const char *name;
		enum build_language lang;
	} names[] = {
		{ "meson.build", build_language_meson },
		// { "CMakeLists.txt", build_language_cmake },
	};

	TSTR(name);
	bool found = false;

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(names); ++i) {
		path_join(wk, &name, cwd, names[i].name);

		if (fs_file_exists(name.buf)) {
			found = true;
			break;
		}
	}

	if (!found) {
		TSTR(name_buf);
		for (i = 0; i < ARRAY_LEN(names); ++i) {
			tstr_pushf(wk, &name_buf, "%s%s", names[i].name, i + 1 == ARRAY_LEN(names) ? "" : ", ");
		}

		vm_error_at(wk, -1, "no build file found in %s (tried %s)", cwd, name_buf.buf);
		return 0;
	}

	*out_lang = names[i].lang;

	if (*out_lang == build_language_cmake) {
		vm_warning_at(wk, -1, "using experimental cmake compat mode, this will probably break");
	}

	return get_cstr(wk, tstr_into_str(wk, &name));
}
