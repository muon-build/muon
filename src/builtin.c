#include "builtin.h"
#include "interpreter.h"
#include "log.h"
#include "options.h"
#include "parser.h"

#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define ARG_TYPE_NULL 1000 // a number higher than any valid node type

static bool
next_arg(struct ast *ast, struct node **arg, const char **kw, struct node **args)
{
	if ((*args)->type == node_empty) {
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
		return true;
	} else {
		*args = NULL;
		return false;
	}
}

struct args_norm { enum node_type type; struct node *val;  };
struct args_kw { const char *key; enum node_type type; struct node *val; };

static bool
typecheck_node(struct node *n, enum node_type type)
{
	if (type != node_any && n->type != type) {
		LOG_W(log_interp, "expected type %s, got %s", node_type_to_s(type), node_to_s(n));
		return false;
	}

	return true;
}

static bool
parse_args(struct ast *ast, struct node *n, struct args_norm _an[],
	struct args_norm ao[], struct args_kw akw[])
{
	const char *kw;
	struct node *arg, *args = get_node(ast, n->r);
	uint32_t i, stage;
	struct args_norm *an[2] = { _an, ao };

	for (stage = 0; stage < 2; ++stage) {
		if (!an[stage]) {
			continue;
		}

		for (i = 0; an[stage][i].type != ARG_TYPE_NULL; ++i) {
			L(log_interp, "parsing stage %d, arg %d", stage, i);
			if (!next_arg(ast, &arg, &kw, &args)) {
				if (stage == 0) { // required
					LOG_W(log_interp, "missing positional arguments");
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

			if (!typecheck_node(arg, an[stage][i].type)) {
				return false;
			}

			an[stage][i].val = arg;
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
					if (!typecheck_node(arg, akw[i].type)) {
						return false;
					}

					akw[i].val = arg;
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
func_project(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	static struct args_norm an[] = { { node_string }, ARG_TYPE_NULL };
	static struct args_norm ao[] = { { node_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_default_options,
		kw_license,
		kw_meson_version,
		kw_subproject_dir,
		kw_version
	};
	static struct args_kw akw[] = {
		[kw_default_options] = { "default_options", node_array },
		[kw_license] = { "license" },
		[kw_meson_version] = { "meson_version", node_string },
		[kw_subproject_dir] = { "subproject_dir", node_string },
		[kw_version] = { "version", node_string },
		0
	};

	if (!parse_args(ast, n, an, ao, akw)) {
		return false;
	}

	if (!typecheck_node(an[0].val, node_string)) {
		return false;
	}
	wk->project.name = an[0].val->tok->data;

	if (ao[0].val) {
		if (!typecheck_node(ao[0].val, node_string)) {
			return false;
		}

		if (strcmp("c", ao[0].val->tok->data) != 0) {
			// TODO ??
			LOG_W(log_interp, "only C is supported");
			return false;
		}
	}

	wk->project.license = akw[kw_license].val->tok->data;
	wk->project.version = akw[kw_version].val->tok->data;

	return true;
}

static bool
todo(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	LOG_W(log_misc, "function '%s' not implemented", get_node(ast, n->l)->tok->data);
	return false;
}

static const struct {
	const char *name;
	builtin_func func;
} functions[] = {
	{ "add_global_arguments", todo },
	{ "add_global_link_arguments", todo },
	{ "add_languages", todo },
	{ "add_project_arguments", todo },
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
	{ "executable", todo },
	{ "files", todo },
	{ "find_library", todo },
	{ "find_program", todo },
	{ "generator", todo },
	{ "get_option", todo },
	{ "get_variable", todo },
	{ "gettext", todo },
	{ "import", todo },
	{ "include_directories", todo },
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
};

bool
builtin_run(struct ast *ast, struct workspace *wk, struct node *n, uint32_t *obj)
{
	char *name = get_node(ast, n->l)->tok->data;
	uint32_t i;
	for (i = 0; functions[i].name; ++i) {
		if (strcmp(functions[i].name, name) == 0) {
			if (!functions[i].func(ast, wk, n, obj)) {
				LOG_W(log_interp, "error in %s(), %s", name, source_location(ast, n->l));
				return false;
			}
			return true;
		}
	}

	LOG_W(log_misc, "builtin function not found: %s", name);
	return false;
}
