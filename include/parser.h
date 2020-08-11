#ifndef BOSON_PARSER_H
#define BOSON_PARSER_H

#include <stddef.h>

struct node_statement;

struct node_root {
	struct node_statement **statements;
	size_t n;
};

struct node_root parse(const char *);

#endif // BOSON_PARSER_H
