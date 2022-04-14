#ifndef MUON_ERROR_H
#define MUON_ERROR_H

#include <assert.h>
#include <stdarg.h>

#include "platform/filesystem.h"

#define UNREACHABLE assert(false && "unreachable")
#define UNREACHABLE_RETURN do { assert(false && "unreachable"); return 0; } while (0)

void error_unrecoverable(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void error_message(struct source *src, uint32_t line, uint32_t col, const char *msg);
void error_messagev(struct source *src, uint32_t line, uint32_t col, const char *fmt, va_list args);
void error_messagef(struct source *src, uint32_t line, uint32_t col, const char *fmt, ...)
__attribute__ ((format(printf, 4, 5)));
#endif
