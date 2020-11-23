#include "function.h"
#include "ast.h"
#include "eval.h"
#include "options.h"
#include "log.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

static void
project(struct environment *env, struct ast_arguments *args)
{
	if (args->args->expressions[0]->type != EXPRESSION_STRING) {
		fatal("project: first argument must be a string literal");
	}

	env->name = calloc(args->args->expressions[0]->data.string->n,
			sizeof(char));
	strncpy(env->name, args->args->expressions[0]->data.string->data,
			args->args->expressions[0]->data.string->n);

	if (args->args->expressions[1]->type != EXPRESSION_STRING) {
		fatal("project: second argument must be a string literal");
	}
	const char *language = args->args->expressions[1]->data.string->data;
	if (strcmp(language, "c") != 0) {
		fatal("project: %s language not supported", language);
	}

	for (size_t i = 0; i < args->kwargs->n; ++i) {
		const char *key = args->kwargs->keys[i]->data;
		struct ast_expression *value = args->kwargs->values[i];
		if (strcmp(key, "version") == 0) {
			if (value->type != EXPRESSION_STRING) {
				fatal("version must be a string");
			}
			env->version = calloc(value->data.string->n,
					sizeof(char));
			strncpy(env->version, value->data.string->data,
					value->data.string->n);
		} else if (strcmp(key, "license") == 0) {
			if (value->type == EXPRESSION_ARRAY) {
				fatal("multiple licenses not supported");
			} else if (value->type != EXPRESSION_STRING) {
				fatal("license must be a string");
			}
			env->version = calloc(value->data.string->n,
					sizeof(char));
			strncpy(env->version, value->data.string->data,
					value->data.string->n);
		} else if (strcmp(key, "default_options") == 0) {
			if (value->type != EXPRESSION_ARRAY) {
				fatal("default_options must be an array");
			}

			for(size_t j = 0; j < value->data.array->n; ++j) {
				struct ast_expression *option =
					value->data.array->expressions[j];

				if (option->type != EXPRESSION_STRING) {
					fatal("option must be a string");
				}

				char k[32] = {0}, v[32] = {0};
				sscanf(option->data.string->data, "%32[^=]=%s",
						k, v);
				if (!options_parse(env->options, k, v)) {
					fatal("failed to parse option '%s=%s'",
							k, v);
				}
			}
		}
	}
}

static void
add_project_arguments(struct environment *env, struct ast_arguments *expr)
{
	for (size_t i = 0; i < expr->args->n; ++i) {

	}
}

static void
todo(struct environment *env, struct ast_arguments *args)
{
	fatal("FUNCTION NOT IMPLEMENTED");
}

static const struct {
	const char *name;
	function impl;
} functions[] = {
	{"add_global_arguments", todo},
	{"add_global_link_arguments", todo},
	{"add_languages", todo},
	{"add_project_arguments", add_project_arguments},
	{"add_project_link_arguments", todo},
	{"add_test_setup", todo},
	{"alias_target", todo},
	{"assert", todo},
	{"benchmark", todo},
	{"both_libraries", todo},
	{"build_target", todo},
	{"configuration_data", todo},
	{"configure_file", todo},
	{"custom_target", todo},
	{"declare_dependency", todo},
	{"dependency", todo},
	{"disabler", todo},
	{"environment", todo},
	{"error", todo},
	{"executable", todo},
	{"files", todo},
	{"find_library", todo},
	{"find_program", todo},
	{"generator", todo},
	{"get_option", todo},
	{"get_variable", todo},
	{"gettext", todo},
	{"import", todo},
	{"include_directories", todo},
	{"install_data", todo},
	{"install_headers", todo},
	{"install_man", todo},
	{"install_subdir", todo},
	{"is_disabler", todo},
	{"is_variable", todo},
	{"jar", todo},
	{"join_paths", todo},
	{"library", todo},
	{"message", todo},
	{"option", todo},
	{"project", project},
	{"run_command", todo},
	{"run_target", todo},
	{"set_variable", todo},
	{"shared_library", todo},
	{"shared_module", todo},
	{"static_library", todo},
	{"subdir", todo},
	{"subdir_done", todo},
	{"subproject", todo},
	{"summary", todo},
	{"test", todo},
	{"vcs_tag", todo},
	{"warning", todo}
};

function
get_function(const char *name)
{
	int low = 0, mid, cmp;
	int high = (sizeof(functions) / sizeof(functions[0])) - 1;
	while (low <= high) {
		mid = (low + high) / 2;
		cmp = strcmp(name, functions[mid].name);
		if (cmp == 0) {
			return functions[mid].impl;
		}
		if (cmp < 0) {
			high = mid - 1;
		}
		else {
			low = mid + 1;
		}
	}

	return NULL;
}
