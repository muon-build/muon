#ifndef BOSON_RUNTIME_H
#define BOSON_RUNTIME_H
#include "workspace.h"

bool eval_entry(struct workspace *wk, const char *src, const char *cwd, const char *build_dir);
bool eval(struct workspace *wk, const char *src);
void print_ast(const char *src);
#endif
