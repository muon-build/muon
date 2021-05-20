#include "posix.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#include "builtin.h"
#include "eval.h"
#include "filesystem.h"
#include "interpreter.h"
#include "log.h"
#include "object.h"
#include "parser.h"

#define ARG_TYPE_NULL 1000 // a number higher than any valid node type

static bool
check_lang(struct workspace *wk, uint32_t id)
{
	if (strcmp("c", wk_objstr(wk, id)) != 0) {
		LOG_W(log_interp, "only c is supported");
		return false;
	}

	return true;
}

static bool
next_arg(struct ast *ast, struct node **arg, const char **kw, struct node **args)
{
	if (!*args || (*args)->type == node_empty) {
		return false;
	}

	assert((*args)->type == node_argument);

	if ((*args)->data == arg_kwarg) {
		*kw = get_node(ast, (*args)->l)->tok->data;
		*arg = get_node(ast, (*args)->r);
	} else {
		*kw = NULL;
		*arg = get_node(ast, (*args)->l);
	}

	/* L(log_interp, "got arg %s:%s", *kw, node_to_s(*arg)); */

	if ((*args)->chflg & node_child_c) {
		*args = get_node(ast, (*args)->c);
	} else {
		*args = NULL;
	}

	return true;
}

struct args_norm { enum obj_type type; uint32_t val; bool set; };
struct args_kw { const char *key; enum obj_type type; uint32_t val; bool set; };

static bool
typecheck(struct obj *o, enum obj_type type)
{
	if (type != obj_any && o->type != type) {
		LOG_W(log_interp, "expected type %s, got %s", obj_type_to_s(type), obj_type_to_s(o->type));
		return false;
	}

	return true;
}

static bool
interp_args(struct ast *ast, struct workspace *wk,
	struct node *args, struct args_norm _an[],
	struct args_norm ao[], struct args_kw akw[])
{
	const char *kw;
	struct node *arg;
	uint32_t i, stage;
	struct args_norm *an[2] = { _an, ao };

	for (stage = 0; stage < 2; ++stage) {
		if (!an[stage]) {
			continue;
		}

		for (i = 0; an[stage][i].type != ARG_TYPE_NULL; ++i) {
			if (!next_arg(ast, &arg, &kw, &args)) {
				if (stage == 0) { // required
					LOG_W(log_interp, "missing positional arguments.  Only got %d", i);
					return false;
				} else if (stage == 1) { // optional
					return true;
				}
			}

			if (kw) {
				if (stage == 0) {
					LOG_W(log_interp, "unexpected kwarg: '%s'", kw);
					return false;
				}

				goto kwargs;
			}

			if (!interp_node(ast, wk, arg, &an[stage][i].val)) {
				return false;
			}

			if (!typecheck(get_obj(wk, an[stage][i].val), an[stage][i].type)) {
				LOG_W(log_interp, "%s arg %d", stage ? "optional" : "positional", i);
				return false;
			}

			an[stage][i].set = true;
		}
	}

	if (akw) {
		while (next_arg(ast, &arg, &kw, &args)) {
			goto process_kwarg;
kwargs:
			if (!akw) {
				LOG_W(log_interp, "this function does not accept kwargs");
				return false;
			}
process_kwarg:
			if (!kw) {
				LOG_W(log_interp, "non-kwarg after kwargs");
				return false;
			}

			for (i = 0; akw[i].key; ++i) {
				if (strcmp(kw, akw[i].key) == 0) {
					if (!interp_node(ast, wk, arg, &akw[i].val)) {
						return false;
					}

					if (!typecheck(get_obj(wk, akw[i].val), akw[i].type)) {
						LOG_W(log_interp, "kwarg %s", akw[i].key);
						return false;
					}

					akw[i].set = true;
					break;
				}
			}

			if (!akw[i].key) {
				LOG_W(log_interp, "invalid kwarg: '%s'", kw);
				return false;
			}
		}


	} else if (next_arg(ast, &arg, &kw, &args)) {
		LOG_W(log_interp, "this function does not accept kwargs");
		return false;
	}

	return true;
}

static bool
func_project(struct ast *ast, struct workspace *wk, uint32_t _, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	static struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_default_options,
		kw_license,
		kw_meson_version,
		kw_subproject_dir,
		kw_version
	};
	static struct args_kw akw[] = {
		[kw_default_options] = { "default_options", obj_array },
		[kw_license] = { "license" },
		[kw_meson_version] = { "meson_version", obj_string },
		[kw_subproject_dir] = { "subproject_dir", obj_string },
		[kw_version] = { "version", obj_string },
		0
	};

	if (!interp_args(ast, wk, args, an, ao, akw)) {
		return false;
	}

	current_project(wk)->cfg.name = get_obj(wk, an[0].val)->dat.str;

	if (ao[0].set && !check_lang(wk, ao[0].val)) {
		return false;
	}

	current_project(wk)->cfg.license = get_obj(wk, akw[kw_license].val)->dat.str;
	current_project(wk)->cfg.version = get_obj(wk, akw[kw_version].val)->dat.str;

	return true;
}

static enum iteration_result
func_add_project_arguments_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct obj *val = get_obj(wk, val_id);
	if (!typecheck(val, obj_string)) {
		return ir_err;
	}

	obj_array_push(wk, current_project(wk)->cfg.args, val_id);

	return ir_cont;
}

static bool
func_add_project_arguments(struct ast *ast, struct workspace *wk, uint32_t _, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };
	enum kwargs { kw_language, };
	static struct args_kw akw[] = {
		[kw_language] = { "language", obj_string },
	};

	if (!interp_args(ast, wk, args, an, NULL, akw)) {
		return false;
	}

	if (akw[kw_language].set) {
		if (!check_lang(wk, akw[kw_language].val)) {
			return false;
		}
	}

	return obj_array_foreach(wk, an[0].val, NULL, func_add_project_arguments_iter);
}

static bool
func_meson_get_compiler(struct ast *ast, struct workspace *wk, uint32_t _, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs { kw_native, };
	static struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
	};

	if (!interp_args(ast, wk, args, an, NULL, akw)) {
		return false;
	}

	if (!check_lang(wk, an[0].val)) {
		return false;
	}

	make_obj(wk, obj, obj_compiler);

	return true;
}

static enum iteration_result
func_compiler_get_supported_arguments_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	uint32_t *arr = _ctx;
	struct obj *val = get_obj(wk, val_id);

	if (!typecheck(val, obj_string)) {
		return ir_err;
	}

	L(log_interp, "TODO: check '%s'", wk_objstr(wk, val_id));

	obj_array_push(wk, *arr, val_id);

	return ir_cont;
}

static bool
func_compiler_get_supported_arguments(struct ast *ast, struct workspace *wk,
	uint32_t _, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_array);

	return obj_array_foreach(wk, an[0].val, obj, func_compiler_get_supported_arguments_iter);
}

static enum iteration_result
func_files_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	uint32_t *arr = _ctx;

	if (!typecheck(get_obj(wk, val_id), obj_string)) {
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

	obj_array_push(wk, *arr, file_id);

	return ir_cont;
}

static bool
func_files(struct ast *ast, struct workspace *wk, uint32_t _, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_array);

	return obj_array_foreach(wk, an[0].val, obj, func_files_iter);
}

static bool
func_include_directories(struct ast *ast, struct workspace *wk, uint32_t _, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	struct obj *file = make_obj(wk, obj, obj_file);

	file->dat.file = wk_str_pushf(wk, "%s/%s", wk_str(wk, current_project(wk)->cwd), wk_objstr(wk, an[0].val));

	return true;
}

static bool
func_declare_dependency(struct ast *ast, struct workspace *wk, uint32_t _, struct node *args, uint32_t *obj)
{
	enum kwargs {
		kw_link_with,
		kw_include_directories,
	};
	static struct args_kw akw[] = {
		[kw_link_with] = { "link_with", obj_array },
		[kw_include_directories] = { "include_directories", obj_file },
		0
	};

	if (!interp_args(ast, wk, args, NULL, NULL, akw)) {
		return false;
	}

	struct obj *dep = make_obj(wk, obj, obj_dependency);

	if (akw[kw_link_with].set) {
		dep->dat.dep.link_with = akw[kw_link_with].val;
	}

	if (akw[kw_include_directories].set) {
		dep->dat.dep.include_directories = akw[kw_include_directories].val;
	}

	return true;
}

static bool
tgt_common(struct ast *ast, struct workspace *wk, struct node *args, uint32_t *obj, enum tgt_type type)
{
	static struct args_norm an[] = { { obj_string }, { obj_array }, ARG_TYPE_NULL };
	enum kwargs {
		kw_include_directories,
		kw_dependencies
	};
	static struct args_kw akw[] = {
		[kw_include_directories] = { "include_directories", obj_file },
		[kw_dependencies] = { "dependencies", obj_array },
		0
	};

	if (!interp_args(ast, wk, args, an, NULL, akw)) {
		return false;
	}

	struct obj *tgt = make_obj(wk, obj, obj_build_target);

	tgt->dat.tgt.type = type;
	tgt->dat.tgt.name = get_obj(wk, an[0].val)->dat.str;
	tgt->dat.tgt.src = an[1].val;

	if (akw[kw_include_directories].set) {
		tgt->dat.tgt.include_directories = akw[kw_include_directories].val;
	}

	if (akw[kw_dependencies].set) {
		tgt->dat.tgt.deps = akw[kw_dependencies].val;
	}

	darr_push(&current_project(wk)->tgts, obj);

	return true;
}

static bool
func_executable(struct ast *ast, struct workspace *wk, uint32_t _, struct node *args, uint32_t *obj)
{
	return tgt_common(ast, wk, args, obj, tgt_executable);
}

static bool
func_library(struct ast *ast, struct workspace *wk, uint32_t _, struct node *args, uint32_t *obj)
{
	return tgt_common(ast, wk, args, obj, tgt_library);
}

static bool
func_message(struct ast *ast, struct workspace *wk, uint32_t rcvr, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	LOG_I(log_misc, "%s", wk_objstr(wk, an[0].val));

	*obj = 0;

	return true;
}

static bool
func_subproject(struct ast *ast, struct workspace *wk, uint32_t rcvr, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	uint32_t parent_project, sub_project;
	struct obj *subproj_obj = make_obj(wk, obj, obj_subproject);
	make_project(wk, &sub_project);
	subproj_obj->dat.subproj = sub_project;

	const char *subproj_name = wk_objstr(wk, an[0].val),
		   *cur_cwd = wk_str(wk, current_project(wk)->cwd);

	char source[PATH_MAX + 1];

	snprintf(source, PATH_MAX, "%s/subprojects/%s/%s", cur_cwd, subproj_name, "meson.build");

	{
		parent_project = wk->cur_project;
		wk->cur_project = sub_project;

		current_project(wk)->cwd = wk_str_pushf(wk, "%s/subprojects/%s", cur_cwd, subproj_name);
		current_project(wk)->build_dir = wk_str_pushf(wk, "%s/subprojects/%s", cur_cwd, subproj_name);

		if (!eval(wk, source)) {
			return false;
		}

		wk->cur_project = parent_project;
	}

	return true;
}

static bool
func_subproject_get_variable(struct ast *ast, struct workspace *wk, uint32_t rcvr, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	const char *name = wk_objstr(wk, an[0].val);
	uint32_t subproj = get_obj(wk, rcvr)->dat.subproj;

	return get_obj_id(wk, name, obj, subproj);
}

static bool
todo(struct ast *ast, struct workspace *wk, uint32_t rcvr_id, struct node *args, uint32_t *obj)
{
	if (rcvr_id) {
		struct obj *rcvr = get_obj(wk, rcvr_id);
		LOG_W(log_misc, "method on %s not implemented", obj_type_to_s(rcvr->type));
	} else {
		LOG_W(log_misc, "function not implemented");
	}
	return false;
}

typedef bool (*builtin_func)(struct ast *ast, struct workspace *wk, uint32_t recvr, struct node *n, uint32_t *obj);

static const struct {
	const char *name;
	builtin_func func;
} funcs[obj_type_count][64 /* increase if needed */] = {
	[obj_default] = {
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
		{ "dependency", todo },
		{ "disabler", todo },
		{ "environment", todo },
		{ "error", todo },
		{ "executable", func_executable },
		{ "files", func_files },
		{ "find_library", todo },
		{ "find_program", todo },
		{ "generator", todo },
		{ "get_option", todo },
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
		{ "option", todo },
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
	},
	[obj_meson] = {
		{ "get_compiler", func_meson_get_compiler },
		{ NULL, NULL },
	},
	[obj_compiler] = {
		{ "get_supported_arguments", func_compiler_get_supported_arguments },
		{ NULL, NULL },
	},
	[obj_subproject] = {
		{ "get_variable", func_subproject_get_variable },
		{ NULL, NULL },
	},
};

bool
builtin_run(struct ast *ast, struct workspace *wk, uint32_t rcvr_id, struct node *n, uint32_t *obj)
{
	const char *name;

	enum obj_type recvr_type;
	struct node *args;

	if (rcvr_id) {
		struct obj *rcvr = get_obj(wk, rcvr_id);
		name = get_node(ast, n->r)->tok->data;
		args = get_node(ast, n->c);
		recvr_type = rcvr->type;
	} else {
		name = get_node(ast, n->l)->tok->data;
		args = get_node(ast, n->r);
		recvr_type = obj_default;
	}

	/* L(log_misc, "calling %s.%s", obj_type_to_s(recvr_type), name); */

	if (recvr_type == obj_null) {
		LOG_W(log_misc, "tried to call %s on null", name);
		return false;
	}

	uint32_t i;
	for (i = 0; funcs[recvr_type][i].name; ++i) {
		if (strcmp(funcs[recvr_type][i].name, name) == 0) {
			if (!funcs[recvr_type][i].func(ast, wk, rcvr_id, args, obj)) {
				LOG_W(log_interp, "error in %s(), %s", name, source_location(ast, n->l));
				return false;
			}
			/* L(log_misc, "finished calling %s.%s", obj_type_to_s(recvr_type), name); */
			return true;
		}
	}

	LOG_W(log_misc, "builtin function not found: %s", name);
	return false;
}
