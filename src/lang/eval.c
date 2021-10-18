#include "posix.h"

#include <limits.h>
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
eval_project(struct workspace *wk, const char *subproject_name,
	const char *cwd, const char *build_dir, uint32_t *proj_id)
{
	char src[PATH_MAX], meson_opts[PATH_MAX],
	     new_cwd[PATH_MAX], new_build_dir[PATH_MAX];

	if (!path_join(src, PATH_MAX, cwd, "meson.build")) {
		return false;
	} else if (!path_join(meson_opts, PATH_MAX, cwd, "meson_options.txt")) {
		return false;
	}

	if (!fs_dir_exists(cwd)) {
		char wrap_path[PATH_MAX], base_path[PATH_MAX];
		snprintf(wrap_path, PATH_MAX, "%s.wrap", cwd);

		if (fs_file_exists(wrap_path)) {
			if (!path_dirname(base_path, PATH_MAX, cwd)) {
				return false;
			}

			struct wrap wrap = { 0 };
			if (!wrap_handle(wrap_path, base_path, &wrap)) {
				return false;
			}

			if (wrap.fields[wf_directory]) {
				if (!path_join(new_cwd, PATH_MAX, base_path, wrap.fields[wf_directory])) {
					return false;
				}

				if (!path_dirname(base_path, PATH_MAX, build_dir)) {
					return false;
				} else if (!path_join(new_build_dir, PATH_MAX, base_path, wrap.fields[wf_directory])) {
					return false;
				}

				cwd = new_cwd;
				build_dir = new_build_dir;

				if (!path_join(src, PATH_MAX, cwd, "meson.build")) {
					return false;
				} else if (!path_join(meson_opts, PATH_MAX, cwd, "meson_options.txt")) {
					return false;
				}
			}

			wrap_destroy(&wrap);
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
