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
	log_warn,
	log_info,
	log_debug,
	log_level_count,
};

enum log_filter {
	log_misc,
	log_mem,
	log_tok,
	log_lex,
	log_parse,
	log_interp,
	log_out,
	log_filter_count,
};

enum log_opts {
	log_show_source = 1 << 0,
};

#define L(...) log_print(__FILE__, __LINE__, __func__, log_debug, __VA_ARGS__)
#define LOG_I(...) log_print(__FILE__, __LINE__, __func__, log_info, __VA_ARGS__)
#define LOG_W(...) log_print(__FILE__, __LINE__, __func__, log_warn, __VA_ARGS__)

void log_bytes_r(const void *src, size_t size);
void log_bytes(const void *src, size_t size);

void log_init(void);
void log_set_file(FILE *logfile);
void log_set_lvl(enum log_level level);
void log_set_filters(enum log_filter filter);
void log_set_opts(enum log_opts opts);

bool log_file_is_a_tty(void);

void log_print(const char *file, uint32_t line, const char *func, enum log_level lvl,
	enum log_filter type, const char *fmt, ...) __attribute__ ((format(printf, 6, 7)));
void log_plain(enum log_level lvl, enum log_filter type, const char *fmt, ...) __attribute__ ((format(printf, 3, 4)));
bool log_filter_name_to_bit(const char *name, uint32_t *res);
uint32_t log_filter_to_bit(enum log_filter f);
#endif
