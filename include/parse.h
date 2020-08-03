#ifndef BOSON_PARSE_H
#define BOSON_PARSE_H

#include <stddef.h>

struct node_statement;

struct node_root {
	struct node_statement *statements;
	size_t n;
};

struct node_root parse(const char *);

#endif // BOSON_PARSE_H
