#ifndef BOSON_PARSE_H
#define BOSON_PARSE_H

#include <stddef.h>

enum node_type {
	NODE_FUNCTION,
};




struct node {
	enum node_type type;
	union {
		struct {
			const char *name;
			size_t n_args;
			struct {
				const char *name;
				const char *value;
			} *args;
		} function;
	} data;
};

struct node *parse(const char *);

#endif // BOSON_PARSE_H
