#ifndef BOSON_RUNTIME_H
#define BOSON_RUNTIME_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

struct workspace;

enum language_mode {
	language_external,
	language_internal,
};

bool eval_entry(enum language_mode mode, struct workspace *wk, const char *src, const char *cwd, const char *build_dir);
bool eval(struct workspace *wk, const char *src);
void error_message(const char *file, uint32_t line, uint32_t col, const char *fmt, va_list args);
void error_messagef(const char *file, uint32_t line, uint32_t col, const char *fmt, ...)
__attribute__ ((format(printf, 4, 5)));
#endif
