#ifndef BOSON_FUNCTION_H
#define BOSON_FUNCTION_H

struct environment;
struct ast_arguments;

typedef int (*function)(struct environment *, struct ast_arguments *);

function get_function(const char *);

#endif // BOSON_FUNCTION_H
