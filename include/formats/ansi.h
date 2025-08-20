/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FORMATS_ANSI_H
#define MUON_FORMATS_ANSI_H

#include "lang/object.h"

#define c_none 0
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

enum ansi_attr {
	ansi_attr_none = c_none,
	ansi_attr_bold = c_bold,
	ansi_attr_underline = c_underline,
	ansi_attr_black = c_black,
	ansi_attr_red = c_red,
	ansi_attr_green = c_green,
	ansi_attr_yellow = c_yellow,
	ansi_attr_blue = c_blue,
	ansi_attr_magenta = c_magenta,
	ansi_attr_cyan = c_cyan,
	ansi_attr_white = c_white,
};

#define CLR1(x) "\033[" STRINGIZE(x) "m"
#define CLR2(x, y) "\033[" STRINGIZE(x) ";" STRINGIZE(y) "m"
#define GET_CLR_MACRO(_1,_2,NAME,...) NAME
#define CLR(...) GET_CLR_MACRO(__VA_ARGS__, CLR2, CLR1,)(__VA_ARGS__)

typedef void ((*parse_ansi_flush_cb)(void *ctx, const struct str *s, uint32_t start, uint32_t len));
typedef void ((*parse_ansi_attr_cb)(void *ctx, enum ansi_attr attr));

void parse_ansi(const struct str *s, void *usr_ctx, parse_ansi_flush_cb flush_cb, parse_ansi_attr_cb attr_cb);
#endif
