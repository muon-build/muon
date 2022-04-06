#include "posix.h"

#include <string.h>

#include "functions/default/options.h"
#include "lang/eval.h"
#include "lang/interpreter.h"
#include "lang/parser.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "wrap.h"

bool
eval_project(struct workspace *wk, const char *subproject_name, const char *cwd,
	const char *build_dir, uint32_t *proj_id)
{
	char src[PATH_MAX], meson_opts[PATH_MAX];

	if (!path_join(src, PATH_MAX, cwd, "meson.build")) {
		return false;
	} else if (!path_join(meson_opts, PATH_MAX, cwd, "meson_options.txt")) {
		return false;
	}

	if (!fs_file_exists(src)) {
		LOG_E("project %s does not contain a meson.build", cwd);
		return false;
	}

	if (subproject_name) {
		log_plain("\n");
		LOG_I("subproject: %s", subproject_name);
	}

	bool ret = false;
	uint32_t parent_project = wk->cur_project;

	make_project(wk, &wk->cur_project, subproject_name, cwd, build_dir);
	*proj_id = wk->cur_project;

	wk->lang_mode = language_opts;
	if (!set_builtin_options(wk)) {
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
eval(struct workspace *wk, struct source *src, obj *res)
{
	L("evaluating '%s'", src->label);
	bool ret = false;
	struct ast ast = { 0 };

	struct source_data *sdata =
		darr_get(&wk->source_data, darr_push(&wk->source_data, &(struct source_data) { 0 }));

	if (!parser_parse(&ast, sdata, src, 0)) {
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
eval_str(struct workspace *wk, const char *str, obj *res)
{
	struct source src = { .label = "<internal>", .src = str, .len = strlen(str) };
	return eval(wk, &src, res);
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
