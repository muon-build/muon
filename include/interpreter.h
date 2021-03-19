#ifndef BOSON_INTERPRETER_H
#define BOSON_INTERPRETER_H

#include <stdbool.h>
#include <stddef.h>

struct options;
struct ast_root;
struct ast_expression;
struct ast_function;
struct ast_string;

enum object_type {
	OBJECT_TYPE_STRING = 1 << 0,
	OBJECT_TYPE_NUMBER = 1 << 1,
	OBJECT_TYPE_BOOLEAN = 1 << 2,
	OBJECT_TYPE_ARRAY = 1 << 3,
};

struct object {
	enum object_type type;
	char *id;
	union {
		struct {
			size_t n;
			char *data;
		} string;
		int number;
		bool boolean;
		struct {
			size_t n;
			struct object **objects;
		} array;
	};
};

char *object_to_str(struct object *);

enum build_target_type {
	BUILD_TARGET_EXECUTABLE = 1 << 0,
	BUILD_TARGET_SHARED = 1 << 1,
	BUILD_TARGET_STATIC = 1 << 2,
};

struct build_target {
	enum build_target_type type;
	struct {
		size_t n;
		char* data;
	} name;

	struct {
		size_t n;
		char **files;
	} source;

	struct {
		size_t n;
		char **paths;
	} include;
};

struct context {
	struct {
		size_t n;
		char *data;
	} version;

	struct options *options;
	struct hash_table *env;

	struct {
		size_t n;
		char **data;
	} project_arguments;

	struct {
		size_t n;
		struct build_target **targets;
	} build;
};

struct object *eval_string(struct ast_string *string);

struct object *eval_meson_object(struct context *, struct ast_function *);

struct object *eval_function(struct context *, struct ast_function *);

struct object *eval_expression(struct context *, struct ast_expression *);

struct context interpret_ast(struct ast_root *);

#endif // BOSON_INTERPRETER_H
