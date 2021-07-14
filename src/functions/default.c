#include "posix.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "coerce.h"
#include "eval.h"
#include "filesystem.h"
#include "functions/common.h"
#include "functions/default.h"
#include "functions/default/configure_file.h"
#include "functions/default/dependency.h"
#include "functions/default/options.h"
#include "functions/default/setup.h"
#include "functions/modules.h"
#include "functions/string.h"
#include "interpreter.h"
#include "log.h"
#include "path.h"
#include "run_cmd.h"
#include "wrap.h"

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
	struct args_norm an[] = { { ARG_TYPE_GLOB }, ARG_TYPE_NULL };
	enum kwargs { kw_language, };
	struct args_kw akw[] = {
		[kw_language] = { "language", obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (akw[kw_language].set) {
		if (!check_lang(wk, akw[kw_language].node, akw[kw_language].val)) {
			return false;
		}
	}

	return obj_array_foreach_flat(wk, an[0].val, &(struct func_add_project_arguments_iter_ctx) {
		.node = an[0].node,
	}, func_add_project_arguments_iter);
}

static bool
func_files(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	return coerce_files(wk, an[0].node, an[0].val, obj);
}

static bool
func_find_program(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_native,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required" },
		[kw_native] = { "native", obj_bool },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum requirement_type requirement;
	if (!coerce_requirement(wk, &akw[kw_required], &requirement)) {
		return false;
	}

	if (requirement == requirement_skip) {
		make_obj(wk, obj, obj_external_program)->dat.external_program.found = false;
		return true;
	}

	char buf[PATH_MAX];
	const char *cmd_path;

	/* TODO: 1. Program overrides set via meson.override_find_program() */
	/* TODO: 2. [provide] sections in subproject wrap files, if wrap_mode is set to forcefallback */
	/* TODO: 3. [binaries] section in your machine files */
	/* TODO: 4. Directories provided using the dirs: kwarg (see below) */
	/* 5. Project's source tree relative to the current subdir */
	/*       If you use the return value of configure_file(), the current subdir inside the build tree is used instead */
	/* 6. PATH environment variable */
	/* TODO: 7. [provide] sections in subproject wrap files, if wrap_mode is set to anything other than nofallback */

	bool found = false;

	if (!path_join(buf, PATH_MAX, wk_str(wk, current_project(wk)->cwd), wk_objstr(wk, an[0].val))) {
		return false;
	}

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
	struct args_norm an[] = { { obj_any }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	return coerce_dirs(wk, an[0].node, an[0].val, obj);
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
		[kw_include_directories] = { "include_directories", obj_any },
		0
	};

	if (!interp_args(wk, args_node, NULL, NULL, akw)) {
		return false;
	}

	if (akw[kw_include_directories].set) {
		uint32_t inc_dirs;

		if (!coerce_dirs(wk, akw[kw_include_directories].node, akw[kw_include_directories].val, &inc_dirs)) {
			return false;
		}

		akw[kw_include_directories].val = inc_dirs;
	}

	struct obj *dep = make_obj(wk, obj, obj_dependency);
	dep->dat.dep.name = wk_str_pushf(wk, "%s:declared_dep", wk_str(wk, current_project(wk)->cfg.name));
	dep->dat.dep.flags |= dep_flag_found;

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
	struct args_norm an[] = { { obj_string }, { ARG_TYPE_GLOB }, ARG_TYPE_NULL };
	enum kwargs {
		kw_sources,
		kw_include_directories,
		kw_dependencies,
		kw_c_args,
		kw_cpp_args,
		kw_objc_args,
		kw_install,
		kw_link_with,
		kw_version,
		kw_build_by_default,
	};
	struct args_kw akw[] = {
		[kw_sources] = { "sources", obj_array },
		[kw_include_directories] = { "include_directories", obj_any },
		[kw_dependencies] = { "dependencies", obj_array },
		[kw_c_args] = { "c_args", obj_array },
		[kw_cpp_args] = { "cpp_args", obj_array },
		[kw_objc_args] = { "objc_args", obj_array },
		[kw_install] = { "install", obj_bool },
		[kw_link_with] = { "link_with", obj_array },
		[kw_version] = { "version", obj_string },
		[kw_build_by_default] = { "build_by_default", obj_bool },
		0
	};

	if (akw[kw_include_directories].set) {
		uint32_t inc_dirs;

		if (!coerce_dirs(wk, akw[kw_include_directories].node, akw[kw_include_directories].val, &inc_dirs)) {
			return false;
		}

		akw[kw_include_directories].val = inc_dirs;
	}

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	uint32_t input;

	if (akw[kw_sources].set) {
		obj_array_extend(wk, an[1].val, akw[kw_sources].val);
	}

	if (!coerce_files(wk, an[1].node, an[1].val, &input)) {
		return false;
	}

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
	default:
		assert(false);
		return false;
	}

	struct obj *tgt = make_obj(wk, obj, obj_build_target);
	tgt->dat.tgt.type = type;
	tgt->dat.tgt.name = get_obj(wk, an[0].val)->dat.str;
	tgt->dat.tgt.src = input;
	tgt->dat.tgt.build_name = wk_str_pushf(wk, "%s%s%s", pref, wk_str(wk, tgt->dat.tgt.name), suff);
	tgt->dat.tgt.cwd = current_project(wk)->cwd;
	tgt->dat.tgt.build_dir = current_project(wk)->build_dir;

	LOG_I(log_interp, "added target %s", wk_str(wk, tgt->dat.tgt.build_name));

	if (akw[kw_include_directories].set) {
		tgt->dat.tgt.include_directories = akw[kw_include_directories].val;
	}

	if (akw[kw_dependencies].set) {
		tgt->dat.tgt.deps = akw[kw_dependencies].val;
	}

	if (akw[kw_c_args].set) {
		tgt->dat.tgt.c_args = akw[kw_c_args].val;
	}

	if (akw[kw_link_with].set) {
		tgt->dat.tgt.link_with = akw[kw_link_with].val;
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
func_assert(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_bool }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	*obj = 0;

	if (!get_obj(wk, an[0].val)->dat.boolean) {
		return false;
	}

	return true;
}

static bool
func_error(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	LOG_W(log_misc, "%s", wk_objstr(wk, an[0].val));
	*obj = 0;

	return false;
}

static bool
func_warning(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	LOG_W(log_misc, "%s", wk_objstr(wk, an[0].val));
	*obj = 0;

	return true;
}

static bool
func_message(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	LOG_I(log_misc, "%s", wk_objstr(wk, an[0].val));
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

	const char *subproj_name = wk_objstr(wk, an[0].val);
	char cwd[PATH_MAX + 1] = { 0 },
	     build_dir[PATH_MAX + 1] = { 0 },
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
	snprintf(cwd, PATH_MAX, "%s/subprojects/%s",
		wk_str(wk, current_project(wk)->source_root), subproj_name);
	snprintf(build_dir, PATH_MAX, "%s/subprojects/%s",
		wk_str(wk, current_project(wk)->build_dir), subproj_name);

	uint32_t subproject_id;

	if (!eval_project(wk, subproject_name_buf, cwd, build_dir, &subproject_id)) {
		return false;
	}

	make_obj(wk, obj, obj_subproject)->dat.subproj = subproject_id;

	return true;
}

#define MAX_ARGS 32
#define ARG_BUF_SIZE 4096
struct run_command_collect_args_ctx {
	char buf[ARG_BUF_SIZE];
	char *argv[MAX_ARGS + 1];
	uint32_t i, argc, err_node;
};

static bool
arg_buf_push(struct workspace *wk, struct run_command_collect_args_ctx *ctx, const char *arg)
{
	uint32_t len = strlen(arg) + 1;

	if (ctx->argc >= MAX_ARGS) {
		interp_error(wk, ctx->err_node, "too many arguments (max: %d)", MAX_ARGS);
		return false;
	} else if (ctx->i + len >= ARG_BUF_SIZE) {
		interp_error(wk, ctx->err_node, "combined arguments exceed maximum length %d", MAX_ARGS);
		return false;
	}

	ctx->argv[ctx->argc] = &ctx->buf[ctx->i + 1];
	strcpy(ctx->argv[ctx->argc], arg);
	++ctx->argc;
	ctx->i += len;
	return true;
}

static enum iteration_result
run_command_collect_args_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct run_command_collect_args_ctx *ctx = _ctx;

	if (!typecheck(wk, ctx->err_node, val, obj_string)) {
		return ir_err;
	}

	if (!arg_buf_push(wk, ctx, wk_objstr(wk, val))) {
		return ir_err;
	}

	return ir_cont;
}

static bool
func_run_command(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_any }, { ARG_TYPE_GLOB }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	struct run_command_collect_args_ctx args_ctx = {
		.err_node = an[1].node, // TODO improve error location reporting
	};

	uint32_t cmd_path;

	switch (get_obj(wk, an[0].val)->type) {
	case obj_string:
		cmd_path = get_obj(wk, an[0].val)->dat.str;
		break;
	case obj_external_program:
		cmd_path = get_obj(wk, an[0].val)->dat.external_program.full_path;
		break;
	default:
		interp_error(wk, an[0].node, "expecting string or external command, got %s",
			obj_type_to_s(get_obj(wk, an[0].val)->type));
		return false;
	}

	if (!arg_buf_push(wk, &args_ctx, wk_str(wk, cmd_path))) {
		return false;
	}

	if (!obj_array_foreach(wk, an[1].val, &args_ctx, run_command_collect_args_iter)) {
		return false;
	}

/* 	uint32_t i; */
/* 	for (i = 0; args_ctx.argv[i]; ++i) { */
/* 		L(log_interp, "%s", args_ctx.argv[i]); */
/* 	} */

	struct run_cmd_ctx cmd_ctx = { 0 };

	if (!run_cmd(&cmd_ctx, wk_str(wk, cmd_path), args_ctx.argv)) {
		if (cmd_ctx.err_msg) {
			interp_error(wk, an[0].node, "error: %s", cmd_ctx.err_msg);
		} else {
			interp_error(wk, an[0].node, "error: %s", strerror(cmd_ctx.err_no));
		}
		return false;
	}

	struct obj *run_result = make_obj(wk, obj, obj_run_result);
	run_result->dat.run_result.status = cmd_ctx.status;
	run_result->dat.run_result.out = wk_str_push(wk, cmd_ctx.out);
	run_result->dat.run_result.err = wk_str_push(wk, cmd_ctx.err);

	return true;
}

static bool
func_subdir(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	char src[PATH_MAX + 1] = { 0 };

	uint32_t old_cwd = current_project(wk)->cwd;
	uint32_t old_build_dir = current_project(wk)->build_dir;

	current_project(wk)->cwd = wk_str_pushf(wk, "%s/%s", wk_str(wk, old_cwd), wk_objstr(wk, an[0].val));
	current_project(wk)->build_dir = wk_str_pushf(wk, "%s/%s", wk_str(wk, old_build_dir), wk_objstr(wk, an[0].val));

	snprintf(src, PATH_MAX, "%s/meson.build", wk_str(wk, current_project(wk)->cwd));

	bool ret = eval_project_file(wk, src);
	current_project(wk)->cwd = old_cwd;
	current_project(wk)->build_dir = old_build_dir;

	return ret;
}

static bool
func_configuration_data(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm ao[] = { { obj_dict }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, NULL, ao, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_configuration_data);

	if (ao[0].set) {
		get_obj(wk, *obj)->dat.configuration_data.dict = ao[0].val;
	} else {
		uint32_t dict;
		make_obj(wk, &dict, obj_dict);
		get_obj(wk, *obj)->dat.configuration_data.dict = dict;
	}

	return true;
}

static bool
func_install_todo(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	L(log_interp, "TODO: installation");
	return true;
}

static bool
func_test(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, { obj_any }, ARG_TYPE_NULL };
	enum kwargs {
		kw_args,
		kw_workdir,
		kw_should_fail,
	};
	struct args_kw akw[] = {
		[kw_args] = { "args", obj_array, },
		[kw_workdir] = { "workdir", obj_string, }, // TODO
		[kw_should_fail] = { "should_fail", obj_bool, },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	uint32_t exe;
	if (!coerce_executable(wk, an[1].node, an[1].val, &exe)) {
		return false;
	}

	uint32_t test_id;
	struct obj *test = make_obj(wk, &test_id, obj_test);
	test->dat.test.name = an[0].val;
	test->dat.test.exe = exe;
	test->dat.test.args = akw[kw_args].val;
	test->dat.test.should_fail =
		akw[kw_should_fail].set
		&& get_obj(wk, akw[kw_should_fail].val)->dat.boolean;

	obj_array_push(wk, current_project(wk)->tests, test_id);
	return true;
}

struct custom_target_cmd_fmt_ctx {
	uint32_t arr, err_node;
	uint32_t input, output;
};

static bool
prefix_plus_index(const char *str, const char *prefix, int64_t *index)
{
	uint32_t len = strlen(prefix);
	if (strlen(str) > len && strncmp(prefix, str, len) == 0) {
		char *endptr;
		*index = strtol(&str[len], &endptr, 10);

		if (*endptr) {
			return false;
		}
		return true;
	}

	return false;
}

enum format_cb_result
format_cmd_arg_cb(struct workspace *wk, uint32_t node, void *_ctx, const char *strkey, uint32_t *elem)
{
	struct custom_target_cmd_fmt_ctx *ctx = _ctx;

	enum cmd_arg_fmt_key {
		key_input,
		key_output,
		key_outdir,
		key_depfile,
		key_plainname,
		key_basename,
		key_private_dir,
		key_source_root,
		key_build_root,
		key_current_source_dir,
		cmd_arg_fmt_key_count,
	};

	const char *key_names[cmd_arg_fmt_key_count] = {
		[key_input             ] = "INPUT",
		[key_output            ] = "OUTPUT",
		[key_outdir            ] = "OUTDIR",
		[key_depfile           ] = "DEPFILE",
		[key_plainname         ] = "PLAINNAME",
		[key_basename          ] = "BASENAME",
		[key_private_dir       ] = "PRIVATE_DIR",
		[key_source_root       ] = "SOURCE_ROOT",
		[key_build_root        ] = "BUILD_ROOT",
		[key_current_source_dir] = "CURRENT_SOURCE_DIR",
	};

	enum cmd_arg_fmt_key key;
	for (key = 0; key < cmd_arg_fmt_key_count; ++key) {
		if (strcmp(key_names[key], strkey) == 0) {
			break;
		}
	}

	uint32_t obj;

	switch (key) {
	case key_input:
		if (!obj_array_index(wk, ctx->input, 0, &obj)) {
			return format_cb_error;
		}

		make_obj(wk, elem, obj_string)->dat.str = get_obj(wk, obj)->dat.file;
		return format_cb_found;
	case key_output:
		if (!obj_array_index(wk, ctx->output, 0, &obj)) {
			return format_cb_error;
		}

		make_obj(wk, elem, obj_string)->dat.str = get_obj(wk, obj)->dat.file;
		return format_cb_found;
	case key_outdir:
		/* @OUTDIR@: the full path to the directory where the output(s)
		 * must be written */
		make_obj(wk, elem, obj_string)->dat.str = current_project(wk)->build_dir;
		return format_cb_found;
	case key_current_source_dir:
		/* @CURRENT_SOURCE_DIR@: this is the directory where the
		 * currently processed meson.build is located in. Depending on
		 * the backend, this may be an absolute or a relative to
		 * current workdir path. */
		make_obj(wk, elem, obj_string)->dat.str = current_project(wk)->cwd;
		return format_cb_found;
	case key_private_dir:
		/* @PRIVATE_DIR@ (since 0.50.1): path to a directory where the
		 * custom target must store all its intermediate files. */
		make_obj(wk, elem, obj_string)->dat.str = wk_str_push(wk, "/tmp");
		return format_cb_found;
	case key_source_root:
	/* @SOURCE_ROOT@: the path to the root of the source tree.
	 * Depending on the backend, this may be an absolute or a
	 * relative to current workdir path. */
	case key_build_root:
	/* @BUILD_ROOT@: the path to the root of the build tree.
	 * Depending on the backend, this may be an absolute or a
	 * relative to current workdir path. */
	case key_plainname:
	/* @PLAINNAME@: the input filename, without a path */
	case key_basename:
	/* @BASENAME@: the input filename, with extension removed */
	case key_depfile:
		/* @DEPFILE@: the full path to the dependency file passed to
		 * depfile */
		LOG_W(log_interp, "TODO: handle @%s@", strkey);
		return format_cb_error;
	default:
		break;
	}


	int64_t index;
	uint32_t arr;

	if (prefix_plus_index(strkey, "INPUT", &index)) {
		arr = ctx->input;
	} else if (prefix_plus_index(strkey, "OUTPUT", &index)) {
		arr = ctx->output;
	} else {
		return format_cb_not_found;
	}

	if (!boundscheck(wk, ctx->err_node, arr, &index)) {
		return format_cb_error;
	} else if (!obj_array_index(wk, arr, index, &obj)) {
		return format_cb_error;
	}

	make_obj(wk, elem, obj_string)->dat.str = get_obj(wk, obj)->dat.file;
	return format_cb_found;
}

static enum iteration_result
custom_target_cmd_fmt_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct custom_target_cmd_fmt_ctx *ctx = _ctx;

	uint32_t str;
	struct obj *obj;

	switch ((obj = get_obj(wk, val))->type) {
	case obj_build_target:
	case obj_external_program:
	case obj_file:
		if (!coerce_executable(wk, ctx->err_node, val, &str)) {
			return ir_err;
		}
		break;
	case obj_string: {
		uint32_t s;
		if (!string_format(wk, ctx->err_node, get_obj(wk, val)->dat.str,
			&s, ctx, format_cmd_arg_cb)) {
			return ir_err;
		}
		make_obj(wk, &str, obj_string)->dat.str = s;
		break;
	}
	default:
		interp_error(wk, ctx->err_node, "unable to coerce '%s' to string", obj_type_to_s(obj->type));
		return ir_err;
	}

	/* L(log_interp, "cmd arg: '%s'", wk_str(wk, str)); */

	obj_array_push(wk, ctx->arr, str);
	return ir_cont;
}

static bool
func_custom_target(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_input,
		kw_output,
		kw_command,
		kw_capture,
		kw_install,
		kw_install_dir,
		kw_build_by_default,
	};
	struct args_kw akw[] = {
		[kw_input]       = { "input", obj_any, },
		[kw_output]      = { "output", obj_any, .required = true },
		[kw_command]     = { "command", obj_array, .required = true },
		[kw_capture]     = { "capture", obj_bool },
		[kw_install]     = { "install", obj_bool }, // TODO
		[kw_install_dir] = { "install_dir", obj_any }, // TODO
		[kw_build_by_default] = { "build_by_default", obj_bool },
		0
	};
	uint32_t input, output, args, cmd, flags = 0;

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	if (akw[kw_input].set) {
		if (!coerce_files(wk, akw[kw_input].node, akw[kw_input].val, &input)) {
			return false;
		} else if (!get_obj(wk, input)->dat.arr.len) {
			interp_error(wk, akw[kw_input].node, "input cannot be empty");
		}
	} else {
		make_obj(wk, &input, obj_array);
	}

	if (!coerce_output_files(wk, akw[kw_output].node, akw[kw_output].val, &output)) {
		return false;
	} else if (!get_obj(wk, output)->dat.arr.len) {
		interp_error(wk, akw[kw_output].node, "output cannot be empty");
	}

	{
		make_obj(wk, &args, obj_array);
		struct custom_target_cmd_fmt_ctx ctx = {
			.arr = args,
			.err_node = akw[kw_command].node,
			.input = input,
			.output = output,
		};

		if (!obj_array_foreach(wk, akw[kw_command].val, &ctx, custom_target_cmd_fmt_iter)) {
			return false;
		}

		if (!get_obj(wk, args)->dat.arr.len) {
			interp_error(wk, akw[kw_command].node, "cmd cannot be empty");
			return false;
		}

		if (!obj_array_index(wk, args, 0, &cmd)) {
			return false;
		}
	}

	if (akw[kw_capture].set && get_obj(wk, akw[kw_capture].val)->dat.boolean) {
		flags |= custom_target_capture;
	}

	struct obj *tgt = make_obj(wk, obj, obj_custom_target);
	tgt->dat.custom_target.name = get_obj(wk, an[0].val)->dat.str;
	LOG_I(log_interp, "adding custom target '%s'", wk_str(wk, tgt->dat.custom_target.name ));
	tgt->dat.custom_target.cmd = cmd;
	tgt->dat.custom_target.args = args;
	tgt->dat.custom_target.input = input;
	tgt->dat.custom_target.output = output;
	tgt->dat.custom_target.flags = flags;

	obj_array_push(wk, current_project(wk)->targets, *obj);
	return true;
}

struct join_paths_ctx {
	uint32_t node;
	char buf[PATH_MAX];
};

static enum iteration_result
join_paths_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct join_paths_ctx *ctx = _ctx;

	if (!typecheck(wk, ctx->node, val, obj_string)) {
		return ir_err;
	}

	char buf[PATH_MAX];
	strcpy(buf, ctx->buf);

	if (!path_join(ctx->buf, PATH_MAX, buf, wk_objstr(wk, val))) {
		return ir_err;
	}

	return ir_cont;
}

static bool
func_join_paths(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB }, ARG_TYPE_NULL };

	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	struct join_paths_ctx ctx = {
		.node = args_node,
	};

	if (!obj_array_foreach(wk, an[0].val, &ctx, join_paths_iter)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, ctx.buf);
	return true;
}

static bool
func_import(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, an, NULL, NULL)) {
		return false;
	}

	enum module mod;
	if (!module_lookup(wk_objstr(wk, an[0].val), &mod)) {
		interp_error(wk, an[0].node, "module not found");
		return false;
	}

	make_obj(wk, obj, obj_module)->dat.module = mod;
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
	{ "assert", func_assert },
	{ "benchmark", todo },
	{ "both_libraries", todo },
	{ "build_target", todo },
	{ "configuration_data", func_configuration_data },
	{ "configure_file", func_configure_file },
	{ "custom_target", func_custom_target },
	{ "declare_dependency", func_declare_dependency },
	{ "dependency", func_dependency },
	{ "disabler", todo },
	{ "environment", todo },
	{ "error", func_error },
	{ "executable", func_executable },
	{ "files", func_files },
	{ "find_library", todo },
	{ "find_program", func_find_program },
	{ "generator", todo },
	{ "get_option", func_get_option },
	{ "get_variable", todo },
	{ "gettext", todo },
	{ "import", func_import },
	{ "include_directories", func_include_directories },
	{ "install_data", func_install_todo },
	{ "install_headers", func_install_todo },
	{ "install_man", func_install_todo },
	{ "install_subdir", func_install_todo },
	{ "is_disabler", todo },
	{ "is_variable", todo },
	{ "jar", todo },
	{ "join_paths", func_join_paths },
	{ "library", func_library },
	{ "message", func_message },
	{ "project", func_project },
	{ "run_command", func_run_command },
	{ "run_target", todo },
	{ "set_variable", todo },
	{ "shared_library", todo },
	{ "shared_module", todo },
	{ "static_library", todo },
	{ "subdir", func_subdir },
	{ "subdir_done", todo },
	{ "subproject", func_subproject },
	{ "summary", todo },
	{ "test", func_test },
	{ "vcs_tag", todo },
	{ "warning", func_warning },
	{ NULL, NULL },
};

const struct func_impl_name impl_tbl_default_external[] = {
	{ "assert", func_assert },
	{ "error", func_error },
	{ "files", func_files },
	{ "join_paths", func_join_paths },
	{ "message", func_message },
	{ "run_command", func_run_command },
	{ "setup", func_setup },
	{ "warning", func_warning },
	{ NULL, NULL },
};

const struct func_impl_name impl_tbl_default_opts[] = {
	{ "option", func_option  },
	{ NULL, NULL },
};
