#include "function.h"
#include "ast.h"
#include "eval.h"
#include "log.h"

#include <stddef.h>
#include <string.h>

/*
static const char *
expr_to_str(enum ast_expression_type type)
{
#define TRANSLATE(e) case e: return #e;
	switch (type) {
	TRANSLATE(EXPRESSION_NONE);
	TRANSLATE(EXPRESSION_ASSIGNMENT);
	TRANSLATE(EXPRESSION_CONDITION);
	TRANSLATE(EXPRESSION_OR);
	TRANSLATE(EXPRESSION_AND);
	TRANSLATE(EXPRESSION_EQUALITY);
	TRANSLATE(EXPRESSION_RELATION);
	TRANSLATE(EXPRESSION_ADDITION);
	TRANSLATE(EXPRESSION_MULTIPLICATION);
	TRANSLATE(EXPRESSION_UNARY);
	TRANSLATE(EXPRESSION_SUBSCRIPT);
	TRANSLATE(EXPRESSION_FUNCTION);
	TRANSLATE(EXPRESSION_METHOD);
	TRANSLATE(EXPRESSION_IDENTIFIER);
	TRANSLATE(EXPRESSION_STRING);
	TRANSLATE(EXPRESSION_ARRAY);
	TRANSLATE(EXPRESSION_BOOL);
	default:
		report("unknown token");
		break;
	}
#undef TRANSLATE
	return "";
}
*/

static int
project(struct environment *env, struct ast_arguments *args)
{
	if (args->args->expressions[0]->type != EXPRESSION_STRING) {
		fatal("project: first argument must be a string literal");
	}

	env->name = args->args->expressions[0]->data.string->data;

	if (args->args->expressions[1]->type != EXPRESSION_STRING) {
		fatal("project: second argument must be a string literal");
	}
	const char *language = args->args->expressions[1]->data.string->data;
	if (strcmp(language, "c") != 0) {
		fatal("project: %s language not supported", language);
	}

	for (size_t i = 0; i < args->kwargs->n; ++i) {

		//info("kwargs %s = %s", *(args->kwargs->keys[i]->data),
		//		*(args->kwargs->values[i])->data);
	}
	//struct ast_keyword_list *kwargs;

	return -1;
}

static int
todo(struct environment *env, struct ast_arguments *args)
{
	fatal("FUNCTION NOT IMPLEMENTED");
	return -1;
}

static const struct {
	const char *name;
	function impl;
} functions[] = {
	{"add_global_arguments", todo},
	{"add_global_link_arguments", todo},
	{"add_languages", todo},
	{"add_project_arguments", todo},
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

	fatal("function not found");
	return NULL;
}
