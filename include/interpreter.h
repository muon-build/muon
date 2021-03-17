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
	OBJECT_TYPE_STRING = 0 << 0,
	OBJECT_TYPE_NUMBER = 0 << 1,
	OBJECT_TYPE_BOOLEAN = 0 << 2,
	OBJECT_TYPE_ARRAY = 0 << 3,
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
};

struct object *eval_string(struct ast_string *string);

struct object *eval_meson_object(struct context *, struct ast_function *);

struct object *eval_function(struct context *, struct ast_function *);

struct object *eval_expression(struct context *, struct ast_expression *);

struct context interpret_ast(struct ast_root *);

#endif // BOSON_INTERPRETER_H
