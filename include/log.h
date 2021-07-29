#ifndef SHARED_UTIL_LOG_H
#define SHARED_UTIL_LOG_H

#include "posix.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

enum log_level {
	log_quiet,
	log_error,
	log_warn,
	log_info,
	log_debug,
	log_level_count,
};

enum log_opts {
	log_show_source = 1 << 0,
};

#define L(...) log_print(__FILE__, __LINE__, __func__, log_debug, __VA_ARGS__)
#define LOG_I(...) log_print(__FILE__, __LINE__, __func__, log_info, __VA_ARGS__)
/* #define LOG_W(...) log_print(__FILE__, __LINE__, __func__, log_warn, __VA_ARGS__) */
#define LOG_E(...) log_print(__FILE__, __LINE__, __func__, log_error, __VA_ARGS__)

void log_bytes_r(const void *src, size_t size);
void log_bytes(const void *src, size_t size);

void log_init(void);
void log_set_file(FILE *logfile);
void log_set_lvl(enum log_level level);
void log_set_opts(enum log_opts opts);

bool log_file_is_a_tty(void);

void log_print(const char *file, uint32_t line, const char *func, enum log_level lvl,
	const char *fmt, ...) __attribute__ ((format(printf, 5, 6)));
bool log_clr(void);
void log_plain(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void log_plainv(const char *fmt, va_list args);
#endif
