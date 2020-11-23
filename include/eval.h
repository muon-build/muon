#ifndef BOSON_INTERPRETER_H
#define BOSON_INTERPRETER_H

struct ast_root;
struct options;

struct environment {
	char *name;
	char *version;
	char *license;

	struct options *options;
};



struct environment eval(struct ast_root *);

#endif // BOSON_INTERPRETER_H
