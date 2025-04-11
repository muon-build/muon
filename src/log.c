/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Simon Zeni <simon@bl4ckb0ne.ca>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "lang/string.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/log.h"
#include "platform/os.h"

const char *log_level_clr[log_level_count] = {
	[log_error] = "31",
	[log_warn] = "33",
	[log_note] = "36",
	[log_info] = "0",
	[log_debug] = "36",
};

const char *log_level_name[log_level_count] = {
	[log_error] = "error",
	[log_warn] = "warning",
	[log_note] = "note",
	[log_info] = "info",
	[log_debug] = "debug",
};

const char *log_level_shortname[log_level_count] = {
	[log_error] = "err ",
	[log_warn] = "warn ",
	[log_note] = "note ",
	[log_info] = "",
	[log_debug] = "dbg ",
};

static struct {
	FILE *file;
	enum log_level level;
	uint32_t filter;
	bool initialized, clr;
	const char *prefix;
	struct tstr *tstr;
} log_cfg = {
	.level = log_info,
};

bool
log_should_print(enum log_level lvl)
{
	return lvl <= log_cfg.level;
}

const char *
log_get_prefix(void)
{
	return log_cfg.prefix;
}

void
log_set_prefix(const char *prefix)
{
	log_cfg.prefix = prefix;
}

uint32_t
log_print_prefix(enum log_level lvl, char *buf, uint32_t size)
{
	uint32_t len = 0;
	assert(log_cfg.initialized);

	if (log_cfg.prefix) {
		len += snprintf(&buf[len], size - len, "%s ", log_cfg.prefix);
	}

	if (*log_level_shortname[lvl]) {
		if (log_cfg.clr) {
			len += snprintf(&buf[len],
				BUF_SIZE_4k - len,
				"\033[%sm%s\033[0m",
				log_level_clr[lvl],
				log_level_shortname[lvl]);
		} else {
			len = strlen(log_level_shortname[lvl]);
			strncpy(buf, log_level_shortname[lvl], BUF_SIZE_4k);
		}
	}

	return len;
}

void
log_print(bool nl, enum log_level lvl, const char *fmt, ...)
{
	static char buf[BUF_SIZE_4k + 3];

	if (log_should_print(lvl)) {
		uint32_t len = log_print_prefix(lvl, buf, BUF_SIZE_4k);

		assert(log_cfg.initialized);

		va_list ap;
		va_start(ap, fmt);
		len += vsnprintf(&buf[len], BUF_SIZE_4k - len, fmt, ap);
		va_end(ap);

		if (nl && len < BUF_SIZE_4k) {
			buf[len] = '\n';
			buf[len + 1] = 0;
		}

		if (log_cfg.clr) {
			print_colorized(log_cfg.file, buf);
		} else if (log_cfg.tstr) {
			tstr_pushn(0, log_cfg.tstr, buf, len);
			tstr_push(0, log_cfg.tstr, '\n');
		} else {
			fputs(buf, log_cfg.file);
		}
	}
}

void
log_plainv(const char *fmt, va_list ap)
{
	static char buf[BUF_SIZE_32k];

	if (log_cfg.clr) {
		vsnprintf(buf, ARRAY_LEN(buf) - 1, fmt, ap);
		print_colorized(log_cfg.file, buf);
	} else if (log_cfg.tstr) {
		tstr_vpushf(0, log_cfg.tstr, fmt, ap);
	} else {
		vfprintf(log_cfg.file, fmt, ap);
	}
}

void
log_plain(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_plainv(fmt, ap);
	va_end(ap);
}

bool
log_clr(void)
{
	return log_cfg.clr;
}

void
log_init(void)
{
	const char *sll;
	uint64_t ll;

	assert(!log_cfg.initialized);
	log_cfg.initialized = true;

	if ((sll = os_get_env("MUON_LOG_LVL"))) {
		ll = strtoul(sll, NULL, 10);
		log_set_lvl(ll);
	}

	log_set_file(stdout);
}

void
log_set_file(FILE *log_file)
{
	log_cfg.file = log_file;
	log_cfg.tstr = 0;
	log_cfg.clr = fs_is_a_tty(log_file);
}

void
log_set_buffer(struct tstr *buf)
{
	assert(buf->flags & tstr_flag_overflow_alloc);

	log_cfg.tstr = buf;
	log_cfg.file = 0;
	log_cfg.clr = false;
}

void
log_set_lvl(enum log_level lvl)
{
	if (lvl > log_level_count) {
		L("attempted to set log level to invalid value %d (max: %d)", lvl, log_level_count);
		return;
	}

	log_cfg.level = lvl;
}

FILE *
_log_file(void)
{
	return log_cfg.file;
}

struct tstr *
_log_tstr(void)
{
	return log_cfg.tstr;
}
