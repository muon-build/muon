#include "posix.h"

#include <limits.h>
#include <string.h>

#include "eval.h"
#include "filesystem.h"
#include "functions/common.h"
#include "functions/default.h"
#include "functions/default/options.h"
#include "interpreter.h"
#include "log.h"
#include "run_cmd.h"
#include "wrap.h"

enum requirement_type {
	requirement_skip,
	requirement_required,
	requirement_auto,
};

static bool
feature_opt_or_bool_to_requirement(struct workspace *wk, struct args_kw *kw_required,
	enum requirement_type *requirement)
{
	if (kw_required->set) {
		if (get_obj(wk, kw_required->val)->type == obj_bool) {
			if (get_obj(wk, kw_required->val)->dat.boolean) {
				*requirement = requirement_required;
			} else {
				*requirement = requirement_auto;
			}
		} else if (get_obj(wk, kw_required->val)->type == obj_feature_opt) {
			switch (get_obj(wk, kw_required->val)->dat.feature_opt.state) {
			case feature_opt_disabled:
				*requirement = requirement_skip;
				break;
			case feature_opt_enabled:
				*requirement = requirement_required;
				break;
			case feature_opt_auto:
				*requirement = requirement_auto;
				break;
			}
		} else {
			interp_error(wk, kw_required->node, "expected type %s or %s, got %s",
				obj_type_to_s(obj_bool),
				obj_type_to_s(obj_feature_opt),
				obj_type_to_s(get_obj(wk, kw_required->val)->type)
				);
			return false;
		}
	} else {
		*requirement = requirement_required;
	}

	return true;
}

static bool
func_project(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_default_options,
		kw_license,
		kw_meson_version,
		kw_subproject_dir,
		kw_version
	};
	struct args_kw akw[] = {
		[kw_default_options] = { "default_options", obj_array },
		[kw_license] = { "license" },
		[kw_meson_version] = { "meson_version", obj_string },
		[kw_subproject_dir] = { "subproject_dir", obj_string },
		[kw_version] = { "version", obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, ao, akw)) {
		return false;
	}

	current_project(wk)->cfg.name = get_obj(wk, an[0].val)->dat.str;

	if (ao[0].set && !check_lang(wk, ao[0].node, ao[0].val)) {
		return false;
	}

	current_project(wk)->cfg.license = get_obj(wk, akw[kw_license].val)->dat.str;
	current_project(wk)->cfg.version = get_obj(wk, akw[kw_version].val)->dat.str;

	return true;
}

struct func_add_project_arguments_iter_ctx {
	uint32_t node;
};

static enum iteration_result
func_add_project_arguments_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct func_add_project_arguments_iter_ctx *ctx = _ctx;

	if (!typecheck(wk, ctx->node, val_id, obj_string)) {
		return ir_err;
	}

	obj_array_push(wk, current_project(wk)->cfg.args, val_id);

	return ir_cont;
}

static bool
func_add_project_arguments(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };
	enum kwargs { kw_language, };
	struct args_kw akw[] = {
		[kw_language] = { "language", obj_string },
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (akw[kw_language].set) {
		if (!check_lang(wk, akw[kw_language].node, akw[kw_language].val)) {
			return false;
		}
	}

	return obj_array_foreach(wk, an[0].val, &(struct func_add_project_arguments_iter_ctx) {
		.node = an[0].node,
	}, func_add_project_arguments_iter);
}

struct func_files_iter_ctx {
	uint32_t arr, node;
};

static enum iteration_result
func_files_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct func_files_iter_ctx  *ctx = _ctx;

	if (!typecheck(wk, ctx->node, val_id, obj_string)) {
		return ir_err;
	}

	uint32_t file_id;
	struct obj *file = make_obj(wk, &file_id, obj_file);

	file->dat.file = wk_str_pushf(wk, "%s/%s", wk_str(wk, current_project(wk)->cwd), wk_objstr(wk, val_id));

	const char *abs_path = wk_str(wk, file->dat.file);

	if (!fs_file_exists(abs_path)) {
		LOG_W(log_interp, "the file '%s' does not exist", abs_path);
		return ir_err;
	}

	obj_array_push(wk, ctx->arr, file_id);

	return ir_cont;
}

static bool
func_files(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_array);

	return obj_array_foreach(wk, an[0].val, &(struct func_files_iter_ctx) {
		.arr = *obj,
		.node = an[0].node,
	}, func_files_iter);
}

static bool
func_find_program(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required" },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum requirement_type requirement;
	if (!feature_opt_or_bool_to_requirement(wk, &akw[kw_required], &requirement)) {
		return false;
	}

	if (requirement == requirement_skip) {
		make_obj(wk, obj, obj_external_program)->dat.external_program.found = false;
		return true;
	}

	char buf[PATH_MAX + 1] = { 0 };
	char *cmd_path;

	/* TODO: 1. Program overrides set via meson.override_find_program() */
	/* TODO: 2. [provide] sections in subproject wrap files, if wrap_mode is set to forcefallback */
	/* TODO: 3. [binaries] section in your machine files */
	/* TODO: 4. Directories provided using the dirs: kwarg (see below) */
	/* 5. Project's source tree relative to the current subdir */
	/*       If you use the return value of configure_file(), the current subdir inside the build tree is used instead */
	/* 6. PATH environment variable */
	/* TODO: 7. [provide] sections in subproject wrap files, if wrap_mode is set to anything other than nofallback */

	bool found = false;

	snprintf(buf, PATH_MAX, "%s/%s", wk_str(wk, current_project(wk)->cwd), wk_objstr(wk, an[0].val));
	if (fs_file_exists(buf)) {
		found = true;
		cmd_path = buf;
	} else if (fs_find_cmd(wk_objstr(wk, an[0].val), &cmd_path)) {
		found = true;
	}

	if (!found) {
		if (requirement == requirement_required) {
			interp_error(wk, an[0].node, "program not found");
			return false;
		}

		make_obj(wk, obj, obj_external_program)->dat.external_program.found = false;
	} else {
		struct obj *external_program = make_obj(wk, obj, obj_external_program);
		external_program->dat.external_program.found = true;
		external_program->dat.external_program.full_path = wk_str_push(wk, cmd_path);
	}

	return true;
}

static bool
func_include_directories(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	struct obj *file = make_obj(wk, obj, obj_file);

	file->dat.file = wk_str_pushf(wk, "%s/%s", wk_str(wk, current_project(wk)->cwd), wk_objstr(wk, an[0].val));

	return true;
}

static bool
func_declare_dependency(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	enum kwargs {
		kw_link_with,
		kw_include_directories,
	};
	struct args_kw akw[] = {
		[kw_link_with] = { "link_with", obj_array },
		[kw_include_directories] = { "include_directories", obj_file },
		0
	};

	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	struct obj *dep = make_obj(wk, obj, obj_dependency);
	dep->dat.dep.name = wk_str_pushf(wk, "%s:declared_dep", wk_str(wk, current_project(wk)->cfg.name));
	dep->dat.dep.found = true;

	if (akw[kw_link_with].set) {
		dep->dat.dep.link_with = akw[kw_link_with].val;
	}

	if (akw[kw_include_directories].set) {
		dep->dat.dep.include_directories = akw[kw_include_directories].val;
	}

	return true;
}

static bool
tgt_common(struct workspace *wk, uint32_t args_node, uint32_t *obj, enum tgt_type type)
{
	struct args_norm an[] = { { obj_string }, { obj_array }, ARG_TYPE_NULL };
	enum kwargs {
		kw_include_directories,
		kw_dependencies,
		kw_c_args,
	};
	struct args_kw akw[] = {
		[kw_include_directories] = { "include_directories", obj_file },
		[kw_dependencies] = { "dependencies", obj_array },
		[kw_c_args] = { "c_args", obj_array },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	struct obj *tgt = make_obj(wk, obj, obj_build_target);

	tgt->dat.tgt.type = type;
	tgt->dat.tgt.name = get_obj(wk, an[0].val)->dat.str;
	tgt->dat.tgt.src = an[1].val;

	const char *pref, *suff;
	switch (type) {
	case tgt_executable:
		pref = "";
		suff = "";
		break;
	case tgt_library:
		pref = "lib";
		suff = ".a";
		break;
	}

	tgt->dat.tgt.build_name = wk_str_pushf(wk, "%s%s%s", pref, wk_str(wk, tgt->dat.tgt.name), suff);

	if (akw[kw_include_directories].set) {
		tgt->dat.tgt.include_directories = akw[kw_include_directories].val;
	}

	if (akw[kw_dependencies].set) {
		tgt->dat.tgt.deps = akw[kw_dependencies].val;
	}

	if (akw[kw_c_args].set) {
		tgt->dat.tgt.c_args = akw[kw_c_args].val;
	}

	obj_array_push(wk, current_project(wk)->targets, *obj);

	return true;
}

static bool
func_executable(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	return tgt_common(wk, args_node, obj, tgt_executable);
}

static bool
func_library(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	return tgt_common(wk, args_node, obj, tgt_library);
}

static bool
func_message(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	fputs(wk_objstr(wk, an[0].val), stdout);
	fflush(stdout);

	*obj = 0;

	return true;
}

#define BASE_PATH_MAX (PATH_MAX / 2)

static bool
func_subproject(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_default_options,
	};
	struct args_kw akw[] = {
		[kw_default_options] = { "default_options", obj_array },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	const char *subproj_name = wk_objstr(wk, an[0].val),
		   *cur_cwd = wk_str(wk, current_project(wk)->cwd);

	char cwd[PATH_MAX + 1] = { 0 },
	     build_dir[PATH_MAX + 1] = { 0 },
	     subproject_dir[BASE_PATH_MAX + 1] = { 0 },
	     subproject_name_buf[PATH_MAX + 1] = { 0 };

	/* copying subproj_name to a buffer and passing that to eval_project
	 * (which in-turn passes it to make_proj) rather than simply passing
	 * subproj_name because subproj_name is stored in the workspace's
	 * string buffer.  This means that if anyone grows that buffer between
	 * the time when make_project uses subproj_name and here, it could
	 * become invalidated
	 *
	 * TODO: refactor make_project to accept wk_strings instead of char *
	 *
	 * The only reason this hasn't been done yet is because it will make it
	 * more messy to call the entry eval_project().
	 */
	strncpy(subproject_name_buf, subproj_name, PATH_MAX);
	snprintf(subproject_dir, BASE_PATH_MAX, "%s/subprojects", cur_cwd);
	snprintf(cwd, PATH_MAX, "%s/%s", subproject_dir, subproj_name);
	snprintf(build_dir, PATH_MAX, "%s/%s", subproject_dir, subproj_name);

	uint32_t subproject_id;

	if (!eval_project(wk, subproject_name_buf, cwd, build_dir, &subproject_id)) {
		return false;
	}

	make_obj(wk, obj, obj_subproject)->dat.subproj = subproject_id;

	return true;
}

static bool
pkg_config(struct workspace *wk, struct run_cmd_ctx *ctx, uint32_t args_node, const char *arg, const char *depname)
{
	if (!run_cmd(ctx, "pkg-config", (char *[]){ "pkg-config", (char *)arg, (char *)depname, NULL })) {
		if (ctx->err_msg) {
			interp_error(wk, args_node, "error: %s", ctx->err_msg);
		} else {
			interp_error(wk, args_node, "error: %s", strerror(ctx->err_no));
		}
		return false;
	}

	return true;
}

static bool
func_dependency(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required" },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum requirement_type requirement;
	if (!feature_opt_or_bool_to_requirement(wk, &akw[kw_required], &requirement)) {
		return false;
	}

	if (requirement == requirement_skip) {
		struct obj *dep = make_obj(wk, obj, obj_dependency);
		dep->dat.dep.name = an[0].val;
		dep->dat.dep.found = false;
		return true;
	}

	struct run_cmd_ctx ctx = { 0 };

	if (!pkg_config(wk, &ctx, an[0].node, "--modversion", wk_objstr(wk, an[0].val))) {
		return false;
	}

	struct obj *dep = make_obj(wk, obj, obj_dependency);
	dep->dat.dep.name = an[0].val;

	if (ctx.status != 0) {
		if (requirement == requirement_required) {
			interp_error(wk, an[0].node, "required dependency not found");
			return false;
		}
		dep->dat.dep.found = false;
		return true;
	}

	dep->dat.dep.found = true;

	if (!pkg_config(wk, &ctx, args_node, "--libs", wk_objstr(wk, an[0].val))) {
		return false;
	}

	if (ctx.status == 0) {
		uint32_t link_with;
		make_obj(wk, &link_with, obj_array);
		get_obj(wk, *obj)->dat.dep.link_with = link_with;

		uint32_t i;
		char *start;
		bool first = false;
		for (i = 0; ctx.out[i]; ++i) {
			if (ctx.out[i] == ' ' || ctx.out[i] == '\t' || ctx.out[i] == '\n') {
				ctx.out[i] = 0;
				if (first) {
					first = false;

					uint32_t str_id;
					make_obj(wk, &str_id, obj_string)->dat.str = wk_str_push(wk, start);

					obj_array_push(wk, link_with, str_id);
				}
				continue;
			} else {
				if (!first) {
					start = &ctx.out[i];
					first = true;
				}
				continue;
			}
		}
		return true;
	}

	/* get_obj(wk, *obj)->dat.dep.link_with = akw[kw_link_with].val; */
	/* get_obj(wk, *obj)->dat.dep.include_directories = akw[kw_include_directories].val; */
	return true;
}

const struct func_impl_name impl_tbl_default[] = {
	{ "add_global_arguments", todo },
	{ "add_global_link_arguments", todo },
	{ "add_languages", todo },
	{ "add_project_arguments", func_add_project_arguments },
	{ "add_project_link_arguments", todo },
	{ "add_test_setup", todo },
	{ "alias_target", todo },
	{ "assert", todo },
	{ "benchmark", todo },
	{ "both_libraries", todo },
	{ "build_target", todo },
	{ "configuration_data", todo },
	{ "configure_file", todo },
	{ "custom_target", todo },
	{ "declare_dependency", func_declare_dependency },
	{ "dependency", func_dependency },
	{ "disabler", todo },
	{ "environment", todo },
	{ "error", todo },
	{ "executable", func_executable },
	{ "files", func_files },
	{ "find_library", todo },
	{ "find_program", func_find_program },
	{ "generator", todo },
	{ "get_option", func_get_option },
	{ "get_variable", todo },
	{ "gettext", todo },
	{ "import", todo },
	{ "include_directories", func_include_directories },
	{ "install_data", todo },
	{ "install_headers", todo },
	{ "install_man", todo },
	{ "install_subdir", todo },
	{ "is_disabler", todo },
	{ "is_variable", todo },
	{ "jar", todo },
	{ "join_paths", todo },
	{ "library", func_library },
	{ "message", func_message },
	{ "project", func_project },
	{ "run_command", todo },
	{ "run_target", todo },
	{ "set_variable", todo },
	{ "shared_library", todo },
	{ "shared_module", todo },
	{ "static_library", todo },
	{ "subdir", todo },
	{ "subdir_done", todo },
	{ "subproject", func_subproject },
	{ "summary", todo },
	{ "test", todo },
	{ "vcs_tag", todo },
	{ "warning", func_message },
	{ NULL, NULL },
};

const struct func_impl_name impl_tbl_default_opts[] = {
	{ "option", func_option  },
	{ NULL, NULL },
};
