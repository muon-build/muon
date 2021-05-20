#ifndef BOSON_RUNTIME_H
#define BOSON_RUNTIME_H

#include <stdarg.h>

#include "workspace.h"

bool eval_entry(struct workspace *wk, const char *src, const char *cwd, const char *build_dir);
bool eval(struct workspace *wk, const char *src);
void error_message(const char *file, uint32_t line, uint32_t col, const char *fmt, va_list args);
#endif
