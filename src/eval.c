#include "posix.h"

#include <limits.h>
#include <stdlib.h> // exit
#include <string.h>

#include "eval.h"
#include "filesystem.h"
#include "functions/default/options.h"
#include "interpreter.h"
#include "log.h"
#include "mem.h"
#include "parser.h"
#include "path.h"
#include "wrap.h"

bool
eval_project(struct workspace *wk, const char *subproject_name,
	const char *cwd, const char *build_dir, uint32_t *proj_id)
{
	char src[PATH_MAX], meson_opts[PATH_MAX];

	if (!path_join(src, PATH_MAX, cwd, "meson.build")) {
		return false;
	} else if (!path_join(meson_opts, PATH_MAX, cwd, "meson_options.txt")) {
		return false;
	}

	if (!fs_dir_exists(cwd)) {
		char wrap[PATH_MAX], base[PATH_MAX];
		snprintf(wrap, PATH_MAX, "%s.wrap", cwd);

		if (fs_file_exists(wrap)) {
			if (!path_dirname(base, PATH_MAX, cwd)) {
				return false;
			}

			if (!wrap_handle(wk, wrap, base)) {
				return false;
			}
		} else {
			LOG_E("project %s not found", cwd);
			return false;
		}
	}

	if (!fs_file_exists(src)) {
		LOG_E("project %s does not contain a meson.build", cwd);
		return false;
	}

	bool ret = false;
	uint32_t parent_project = wk->cur_project;

	make_project(wk, &wk->cur_project, subproject_name, cwd, build_dir);
	*proj_id = wk->cur_project;

	wk->lang_mode = language_opts;
	if (!set_default_options(wk)) {
		goto cleanup;
	}

	if (fs_file_exists(meson_opts)) {
		if (!eval_project_file(wk, meson_opts)) {
			goto cleanup;
		}
	}

	if (!check_invalid_option_overrides(wk)) {
		goto cleanup;
	}

	wk->lang_mode = language_external;

	if (!eval_project_file(wk, src)) {
		goto cleanup;
	}

	if (wk->cur_project == 0 && !check_invalid_subproject_option(wk)) {
		goto cleanup;
	}

	ret = true;
cleanup:
	wk->cur_project = parent_project;
	return ret;
}

bool
eval(struct workspace *wk, struct source *src, uint32_t *obj)
{
	bool ret = false;
	struct ast ast = { 0 };

	struct source_data *sdata =
		darr_get(&wk->source_data, darr_push(&wk->source_data, &(struct source_data) { 0 }));

	if (!parser_parse(&ast, sdata, src)) {
		goto ret;
	}

	struct source *old_src = wk->src;
	struct ast *old_ast = wk->ast;

	wk->src = src;
	wk->ast = &ast;

	ret = interp_node(wk, wk->ast->root, obj);

	wk->src = old_src;
	wk->ast = old_ast;
ret:
	ast_destroy(&ast);
	return ret;
}

bool
eval_str(struct workspace *wk, const char *str, uint32_t *obj)
{
	struct source src = { .label = "<internal>", .src = str, .len = strlen(str) };
	return eval(wk, &src, obj);
}

bool
eval_project_file(struct workspace *wk, const char *path)
{
	/* L("evaluating '%s'", src); */
	bool ret = false;
	{
		uint32_t id;
		make_obj(wk, &id, obj_string)->dat.str = wk_str_push(wk, path);
		obj_array_push(wk, wk->sources, id);
	}

	struct source src = { 0 };
	if (!fs_read_entire_file(path, &src)) {
		return false;
	}

	uint32_t obj;
	if (!eval(wk, &src, &obj)) {
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
error_unrecoverable(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_plainv(fmt, ap);
	log_plain("\n");
	va_end(ap);

	exit(1);
}

void
error_message(struct source *src, uint32_t line,
	uint32_t col, const char *fmt, va_list args)
{
	const char *label = log_clr() ? "\033[31merror:\033[0m" : "error:";

	log_plain("%s:%d:%d: %s ", src->label, line, col, label);
	log_plainv(fmt, args);
	log_plain("\n");

	uint64_t i, cl = 1, sol = 0;
	for (i = 0; i < src->len; ++i) {
		if (src->src[i] == '\n') {
			++cl;
			sol = i + 1;
		}

		if (cl == line) {
			break;
		}
	}

	log_plain("%3d | ", line);
	for (i = sol; src->src[i] && src->src[i] != '\n'; ++i) {
		if (src->src[i] == '\t') {
			log_plain("        ");
		} else {
			putc(src->src[i], stderr);
		}
	}
	log_plain("\n");

	log_plain("      ");
	for (i = 0; i < col; ++i) {
		if (src->src[sol + i] == '\t') {
			log_plain("        ");
		} else {
			log_plain(i == col - 1 ? "^" : " ");
		}
	}
	log_plain("\n");
}

void
error_messagef(struct source *src, uint32_t line, uint32_t col, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	error_message(src, line, col, fmt, ap);
	va_end(ap);
}
