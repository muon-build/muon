#ifndef MUON_ERROR_H
#define MUON_ERROR_H

#include <assert.h>
#include <stdarg.h>

#include "log.h"
#include "platform/filesystem.h"

#define UNREACHABLE assert(false && "unreachable")
#define UNREACHABLE_RETURN do { assert(false && "unreachable"); return 0; } while (0)

void error_unrecoverable(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void error_message(struct source *src, uint32_t line, uint32_t col, enum log_level lvl, const char *msg);
void error_messagev(struct source *src, uint32_t line, uint32_t col, enum log_level lvl, const char *fmt, va_list args);
void error_messagef(struct source *src, uint32_t line, uint32_t col, enum log_level lvl, const char *fmt, ...)
__attribute__ ((format(printf, 5, 6)));

void error_diagnostic_store_init(void);
void error_diagnostic_store_replay(bool errors_only);
void error_diagnostic_store_push(uint32_t src_idx, uint32_t line, uint32_t col,
	enum log_level lvl, const char *msg);
uint32_t error_diagnostic_store_push_src(struct source *src);
#endif
