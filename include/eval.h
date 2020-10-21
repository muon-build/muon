#ifndef BOSON_INTERPRETER_H
#define BOSON_INTERPRETER_H

struct hash_table;
struct ast_root;

enum buildtype {
	BUILDTYPE_PLAIN = 1 << 0,
	BUILDTYPE_DEBUG = 1 << 1,
	BUILDTYPE_DEBUGOPTIMIZED = 1 << 2,
	BUILDTYPE_RELEASE = 1 << 3,
	BUILDTYPE_MINSIZE = 1 << 4,
};

struct environment {
	const char *name;
	const char *version;
	struct {
		enum buildtype buildtype;
	} options;
};

struct environment eval(struct ast_root *);

#endif // BOSON_INTERPRETER_H
