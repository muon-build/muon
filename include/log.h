/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LOG_H
#define MUON_LOG_H

#include "compat.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "lang/types.h"

extern const char *log_level_clr[log_level_count];
extern const char *log_level_name[log_level_count];
extern const char *log_level_shortname[log_level_count];

#define L(...) log_print(true, log_debug, __VA_ARGS__)
#define LOG_N(...) log_print(true, log_note, __VA_ARGS__)
#define LOG_I(...) log_print(true, log_info, __VA_ARGS__)
#define LOG_W(...) log_print(true, log_warn, __VA_ARGS__)
#define LOG_E(...) log_print(true, log_error, __VA_ARGS__)

#define LL(...) log_print(false, log_debug, __VA_ARGS__)
#define LLOG_I(...) log_print(false, log_info, __VA_ARGS__)
#define LLOG_W(...) log_print(false, log_warn, __VA_ARGS__)
#define LLOG_E(...) log_print(false, log_error, __VA_ARGS__)

struct tstr;
struct workspace;

void log_set_file(struct workspace *wk, FILE *log_file);
void log_set_debug_file(FILE *log_file);
void log_set_buffer(struct workspace *wk, struct tstr *buf);
void log_set_lvl(enum log_level lvl);
void log_set_indent(uint32_t n);
void log_inc_indent(int32_t n);

void log_progress_enable(struct workspace *wk);
void log_progress_disable(void);
bool log_is_progress_bar_enabled(void);
void log_progress_push_level(double start, double end);
void log_progress_pop_level(void);
void log_progress_push_state(struct workspace *wk);
void log_progress_pop_state(struct workspace *wk);
void log_progress_inc(struct workspace *wk);
void log_progress(struct workspace *wk, double val);
void log_progress_subval(struct workspace *wk, double val, double sub_val);
struct log_progress_style {
	const char *name;
	void (*decorate)(void *usr_ctx, uint32_t w);
	void *usr_ctx;
	double rate_limit;
	uint32_t name_pad;
	bool show_count;
	bool dont_disable_on_error;
};
void log_progress_set_style(const struct log_progress_style *style);

void log_printn(enum log_level lvl, const char *buf, uint32_t len);
void log_printv(enum log_level lvl, const char *fmt, va_list ap);
void log_print(bool nl, enum log_level lvl, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);
void
log_print_middle_truncated(enum log_level lvl, const struct str *buf, const struct str *sep, uint32_t truncate_limit);
void log_plain(enum log_level lvl, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 2, 3);
void log_raw(const char *fmt, ...) MUON_ATTR_FORMAT(printf, 1, 2);
void log_rawv(const char *fmt, va_list ap);
bool log_should_print(enum log_level lvl);
void log_flush(void);

void log_plain_version_string(enum log_level lvl, const char *version);
const char *bool_to_yn(bool v);

// You should probably not use this.  Prefer to go through one of the above functions
FILE *_log_file(void);
#endif
