#include "posix.h"

#include <assert.h>
#include <string.h>

#include "builtin.h"
#include "filesystem.h"
#include "interpreter.h"
#include "log.h"
#include "object.h"
#include "parser.h"

#define ARG_TYPE_NULL 1000 // a number higher than any valid node type

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
					LOG_W(log_interp, "unexpected kwarg");
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
func_project(struct ast *ast, struct workspace *wk, struct obj *_recvr, struct node *args, uint32_t *obj)
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

	wk->project.name = get_obj(wk, an[0].val)->dat.s;

	if (ao[0].set) {
		if (strcmp("c", get_obj(wk, ao[0].val)->dat.s) != 0) {
			LOG_W(log_interp, "only c is supported");
			return false;
		}
	}

	wk->project.license = get_obj(wk, akw[kw_license].val)->dat.s;
	wk->project.version = get_obj(wk, akw[kw_version].val)->dat.s;

	return true;
}

static enum iteration_result
func_add_project_arguments_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct obj *val = get_obj(wk, val_id);
	if (!typecheck(val, obj_string)) {
		return ir_err;
	}

	L(log_interp, "TODO: add argument '%s'", val->dat.s);
	return ir_cont;
}

static bool
func_add_project_arguments(struct ast *ast, struct workspace *wk, struct obj *_recvr, struct node *args, uint32_t *obj)
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
		if (strcmp("c", get_obj(wk, akw[kw_language].val)->dat.s) != 0) {
			LOG_W(log_interp, "only c is supported");
			return false;
		}
	}

	return obj_array_foreach(wk, an[0].val, NULL, func_add_project_arguments_iter);
}

static bool
func_meson_get_compiler(struct ast *ast, struct workspace *wk, struct obj *meson, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs { kw_native, };
	static struct args_kw akw[] = {
		[kw_native] = { "native", obj_bool },
	};

	if (!interp_args(ast, wk, args, an, NULL, akw)) {
		return false;
	}

	if (strcmp("c", get_obj(wk, an[0].val)->dat.s) != 0) {
		LOG_W(log_interp, "only c is supported");
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

	L(log_interp, "TODO: check '%s'", val->dat.s);

	obj_array_push(wk, *arr, val_id);

	return ir_cont;
}

static bool
func_compiler_get_supported_arguments(struct ast *ast, struct workspace *wk,
	struct obj *compiler, struct node *args, uint32_t *obj)
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
	struct obj *val = get_obj(wk, val_id);

	if (!typecheck(val, obj_string)) {
		return ir_err;
	} else if (!fs_file_exists(val->dat.s)) {
		LOG_W(log_interp, "the file '%s' does not exist", val->dat.s);
		return ir_err;
	}

	uint32_t file_id;
	struct obj *file = make_obj(wk, &file_id, obj_file);
	file->dat.f = get_obj(wk, val_id)->dat.s;

	obj_array_push(wk, *arr, file_id);

	return ir_cont;
}

static bool
func_files(struct ast *ast, struct workspace *wk,
	struct obj *compiler, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_array }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_array);

	return obj_array_foreach(wk, an[0].val, obj, func_files_iter);
}

static bool
func_include_directories(struct ast *ast, struct workspace *wk,
	struct obj *compiler, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };

	if (!interp_args(ast, wk, args, an, NULL, NULL)) {
		return false;
	}

	// TODO make an "include_directories" object
	*obj = an[0].val;

	return true;
}

static bool
func_executable(struct ast *ast, struct workspace *wk,
	struct obj *compiler, struct node *args, uint32_t *obj)
{
	static struct args_norm an[] = { { obj_string }, { obj_array }, ARG_TYPE_NULL };
	enum kwargs {
		kw_include_directories
	};
	static struct args_kw akw[] = {
		[kw_include_directories] = { "include_directories", obj_string },
		0
	};

	if (!interp_args(ast, wk, args, an, NULL, akw)) {
		return false;
	}

	struct obj *tgt = make_obj(wk, obj, obj_build_target);

	tgt->dat.tgt.name = get_obj(wk, an[0].val)->dat.s;
	tgt->dat.tgt.src = an[1].val;
	tgt->dat.tgt.include_directories = an[2].val;

	darr_push(&wk->tgts, obj);

	return true;
}

static bool
todo(struct ast *ast, struct workspace *wk, struct obj *rcvr, struct node *args, uint32_t *obj)
{
	if (rcvr) {
		LOG_W(log_misc, "method on %s not implemented", obj_type_to_s(rcvr->type));
	} else {
		LOG_W(log_misc, "function not implemented");
	}
	return false;
}

typedef bool (*builtin_func)(struct ast *ast, struct workspace *wk, struct obj *recvr, struct node *n, uint32_t *obj);

static const struct {
	const char *name;
	builtin_func func;
} funcs[obj_type_count][64 /* increase if needed */] = {
	[obj_none] = {
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
		{ "declare_dependency", todo },
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
		{ "library", todo },
		{ "message", todo },
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
		{ "subproject", todo },
		{ "summary", todo },
		{ "test", todo },
		{ "vcs_tag", todo },
		{ "warning", todo },
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
};

bool
builtin_run(struct ast *ast, struct workspace *wk, struct obj *recvr, struct node *n, uint32_t *obj)
{
	char *name;

	enum obj_type recvr_type;
	struct node *args;

	if (recvr) {
		name = get_node(ast, n->r)->tok->data;
		args = get_node(ast, n->c);
		recvr_type = recvr->type;
	} else {
		name = get_node(ast, n->l)->tok->data;
		args = get_node(ast, n->r);
		recvr_type = obj_none;
	}

	uint32_t i;
	for (i = 0; funcs[recvr_type][i].name; ++i) {
		if (strcmp(funcs[recvr_type][i].name, name) == 0) {
			if (!funcs[recvr_type][i].func(ast, wk, recvr, args, obj)) {
				LOG_W(log_interp, "error in %s(), %s", name, source_location(ast, n->l));
				return false;
			}
			return true;
		}
	}

	LOG_W(log_misc, "builtin function not found: %s", name);
	return false;
}
