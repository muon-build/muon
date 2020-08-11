#ifndef BOSON_PARSER_H
#define BOSON_PARSER_H

#include <stddef.h>

struct ast_statement;

struct ast_root {
	struct ast_statement **statements;
	size_t n;
};

struct ast_root parse(const char *);

#endif // BOSON_PARSER_H
