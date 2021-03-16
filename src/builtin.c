#include "ast.h"
#include "interpreter.h"
#include "options.h"
#include "log.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

static struct object *
project(struct context *ctx, struct ast_arguments *args)
{
	if (args->args->expressions[0]->type != EXPRESSION_STRING) {
		fatal("project: first argument must be a string literal");
	}

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
			if (ctx->version.data) {
				fatal("version has already been specified");
			}
			ctx->version.data = calloc(value->data.string->n,
					sizeof(char));
			strncpy(ctx->version.data, value->data.string->data,
					value->data.string->n);
			ctx->version.n = value->data.string->n;
		} else if (strcmp(key, "license") == 0) {
			if (value->type == EXPRESSION_ARRAY) {
				fatal("multiple licenses not supported");
			} else if (value->type != EXPRESSION_STRING) {
				fatal("license must be a string");
			}
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
				if (!options_parse(ctx->options, k, v)) {
					fatal("failed to parse option '%s=%s'",
							k, v);
				}
			}
		}
	}

	return NULL;
}

static struct object *
add_project_arguments(struct context *ctx, struct ast_arguments *args)
{
	const char *language = NULL;
	for (size_t i = 0; i < args->kwargs->n; ++i) {
		const char *key = args->kwargs->keys[i]->data;
		struct ast_expression *value = args->kwargs->values[i];
		if (strcmp(key, "language") == 0) {
			if (value->type != EXPRESSION_STRING) {
				fatal("language must be a string");
			}
			if (language) {
				fatal("language has already been specified");
			}
			language = value->data.string->data;
		} else {
			fatal("invalid keyword argument '%s'", key);
		}
	}

	if (!language) {
		fatal("missing language definition in 'add_project_arguments'");
	} else if (strcmp(language, "c") != 0) {
		fatal("language '%s' is not supported", language);
	}

	for (size_t i = 0; i < args->args->n; ++i) {
		struct ast_expression *expr = args->args->expressions[i];
		struct object *obj = eval_expression(ctx, expr);

		const size_t new_size = ctx->project_arguments.n + 1;
		ctx->project_arguments.data = realloc(
				ctx->project_arguments.data,
				new_size * sizeof(char*));
		ctx->project_arguments.data[ctx->project_arguments.n] =
			object_to_str(obj);
		ctx->project_arguments.n = new_size;

		free(obj);
	}

	return NULL;
}

static struct object *
todo(struct context *ctx, struct ast_arguments *args)
{
	fatal("FUNCTION NOT IMPLEMENTED");
	return NULL;
}

static const struct {
	const char *name;
	struct object *(*impl)(struct context *ctx, struct ast_arguments *);
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

struct object *
eval_function(struct context *ctx, struct ast_function *function)
{
	const char *name = function->left->data;
	info("builtin function '%s'", name);
	int low = 0, mid, cmp;
	int high = (sizeof(functions) / sizeof(functions[0])) - 1;
	while (low <= high) {
		mid = (low + high) / 2;
		cmp = strcmp(name, functions[mid].name);
		if (cmp == 0) {
			return functions[mid].impl(ctx, function->right);
		}
		if (cmp < 0) {
			high = mid - 1;
		}
		else {
			low = mid + 1;
		}
	}

	fatal("builtin function not found: %s", name);
	return NULL;
}
