#ifndef BOSON_INTERPRETER_H
#define BOSON_INTERPRETER_H

#include <stdbool.h>
#include <stddef.h>

#include "lexer.h"
#include "parser.h"

struct workspace {
	struct {
		const char *name;
		const char *version;
		const char *license;
		const char *meson_version;
	} project;

	struct hdarr *objects;
};

bool interpret(struct ast *ast, struct workspace *wk);
struct node *get_node(struct ast *ast, uint32_t i);
#endif
