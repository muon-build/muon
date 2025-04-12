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
#include "preprocessor_helpers.h"

extern const char *log_level_clr[log_level_count];
extern const char *log_level_name[log_level_count];
extern const char *log_level_shortname[log_level_count];

#define c_bold 1
#define c_underline 4
#define c_black 30
#define c_red 31
#define c_green 32
#define c_yellow 33
#define c_blue 34
#define c_magenta 35
#define c_cyan 36
#define c_white 37

#define CLR1(x) "\033[" STRINGIZE(x) "m"
#define CLR2(x, y) "\033[" STRINGIZE(x) ";" STRINGIZE(y) "m"
#define GET_CLR_MACRO(_1,_2,NAME,...) NAME
#define CLR(...) GET_CLR_MACRO(__VA_ARGS__, CLR2, CLR1,)(__VA_ARGS__)

#define L(...) log_print(true, log_debug, __VA_ARGS__)
#define LOG_N(...) log_print(true, log_note, __VA_ARGS__)
#define LOG_I(...) log_print(true, log_info, __VA_ARGS__)
#define LOG_W(...) log_print(true, log_warn, __VA_ARGS__)
#define LOG_E(...) log_print(true, log_error, __VA_ARGS__)

#define LL(...) log_print(false, log_debug, __VA_ARGS__)
#define LLOG_I(...) log_print(false, log_info, __VA_ARGS__)
#define LLOG_W(...) log_print(false, log_warn, __VA_ARGS__)
#define LLOG_E(...) log_print(false, log_error, __VA_ARGS__)

void log_init(void);
void log_set_file(FILE *log_file);
struct tstr;
void log_set_buffer(struct tstr *buf);
void log_set_lvl(enum log_level lvl);
void log_set_prefix(const char *prefix);
const char *log_get_prefix(void);

uint32_t log_print_prefix(enum log_level lvl, char *buf, uint32_t size);
void log_print(bool nl, enum log_level lvl, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);
void log_plain(const char *fmt, ...) MUON_ATTR_FORMAT(printf, 1, 2);
void log_plainv(const char *fmt, va_list ap);
bool log_should_print(enum log_level lvl);

FILE *_log_file(void);
struct tstr *_log_tstr(void);
#endif
