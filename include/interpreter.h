#ifndef BOSON_INTERPRETER_H
#define BOSON_INTERPRETER_H

struct ast_root;

struct rules {
	char *name;
	char *version;
	char *license;
};

struct rules interprete(struct ast_root *);

#endif // BOSON_INTERPRETER_H
