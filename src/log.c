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
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/log.h"
#include "platform/os.h"
#include "platform/term.h"

const char *log_level_clr[log_level_count] = {
	[log_error] = STRINGIZE(c_red),
		[log_warn] = STRINGIZE(c_yellow),
			[log_note] = STRINGIZE(c_cyan),
				[log_info] = "0",
					[log_debug] = STRINGIZE(c_cyan),
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

struct log_progress_lvl {
	double pos, end;
};

struct log_progress {
	struct log_progress_lvl stack[64];

	const char *prev_name;
	struct log_progress_style style;

	double sum_done;
	double sum_total;
	double rate_limit;

	uint32_t len;
	uint32_t width;

	bool init;
};

static struct {
	FILE *file, *debug_file;
	enum log_level level;
	uint32_t filter;
	bool file_is_a_tty, progress_bar;
	uint32_t prefix;
	struct tstr *tstr;
	struct log_progress progress;
} log_cfg = {
	.level = log_info,
};

void
log_set_progress_bar_enabled(bool v)
{
	log_cfg.progress_bar = v;
}

bool
log_is_progress_bar_enabled(void)
{
	return log_cfg.progress_bar;
}

void
log_progress_set_style(const struct log_progress_style *style)
{
	log_cfg.progress.style = *style;
}

void
log_progress_reset(double rate_limit, const char *name)
{
	if (!log_cfg.progress_bar) {
		return;
	}

	log_cfg.progress = (struct log_progress){
		.init = true,
		.rate_limit = rate_limit,
		.style = {
			.name = name,
		},
	};

	if (log_cfg.file_is_a_tty) {
		int term_fd;
		if (fs_fileno(log_cfg.file, &term_fd)) {
			uint32_t _h;
			term_winsize(term_fd, &_h, &log_cfg.progress.width);
		}
	}
}

void
log_progress_push_level(double start, double end)
{
	struct log_progress *lp = &log_cfg.progress;
	if (!lp->init) {
		return;
	}

	if (lp->len >= ARRAY_LEN(lp->stack)) {
		return;
	}
	lp->stack[lp->len] = (struct log_progress_lvl){
		.pos = start,
		.end = end,
	};
	++lp->len;

	lp->sum_total += end - start;
}

void
log_progress_pop_level(void)
{
	struct log_progress *lp = &log_cfg.progress;
	if (!lp->init) {
		return;
	}

	if (lp->len) {
		--lp->len;
		lp->sum_done += lp->stack[lp->len].end - lp->stack[lp->len].pos;
	}
}

void
log_progress_inc(struct workspace *wk)
{
	struct log_progress *lp = &log_cfg.progress;
	if (!lp->init) {
		return;
	}

	log_progress(wk, lp->stack[lp->len - 1].pos + 1);
}

void
log_progress_print_bar(const char *name)
{
	struct log_progress *lp = &log_cfg.progress;

	uint32_t pad = 0;
	char info[BUF_SIZE_4k];

	pad += snprintf(info + pad, sizeof(info) - pad, " ");

	if (lp->style.show_count) {
		pad += snprintf(
			info + pad, sizeof(info) - pad, "%3d/%3d ", (uint32_t)lp->sum_done, (uint32_t)lp->sum_total);
	}

	char fmt[16];
	snprintf(fmt, sizeof(fmt), "%%-%d.%ds", lp->style.name_pad, lp->style.name_pad);
	pad += snprintf(info + pad, sizeof(info) - pad, fmt, name ? name : "");

	pad += 2;

	const double pct_done = lp->sum_done / lp->sum_total * (double)(lp->width - pad);

	log_raw("\033[K[");

	uint32_t i;
	for (i = 0; i < lp->width - pad; ++i) {
		if (i <= pct_done && i + 1 >= pct_done) {
			fputc('=', log_cfg.file);
		} else if (i < pct_done) {
			fputc('=', log_cfg.file);
		} else {
			fputc(' ', log_cfg.file);
		}
	}

	log_raw("]%s\r", info);

	fflush(log_cfg.file);
}

void
log_progress(struct workspace *wk, double val)
{
	if (!log_cfg.progress_bar) {
		return;
	}

	struct log_progress *lp = &log_cfg.progress;
	if (!lp->init) {
		return;
	}

	struct log_progress_lvl *cur = &lp->stack[lp->len - 1];

	if (!(cur->pos < val && val <= cur->end)) {
		return;
	}

	const double diff = val - cur->pos;

	if (diff < lp->rate_limit) {
		return;
	}
	cur->pos = val;

	lp->sum_done += diff;

	const char *name = lp->style.name;
	if (!name && wk->projects.len) {
		obj proj_name = current_project(wk)->cfg.name;
		if (proj_name) {
			name = get_str(wk, proj_name)->s;
		}
	}

	lp->prev_name = name;

	log_progress_print_bar(name);
}

bool
log_should_print(enum log_level lvl)
{
	return lvl <= log_cfg.level;
}

void
log_set_prefix(int32_t n)
{
	log_cfg.prefix += n;
}

static uint32_t
log_print_prefix(enum log_level lvl, char *buf, uint32_t size)
{
	uint32_t len = 0;

	for (uint32_t i = 0; i < log_cfg.prefix; ++i) {
		len += snprintf(&buf[len], size - len, " ");
	}

	if (*log_level_shortname[lvl]) {
		len += snprintf(
			&buf[len], BUF_SIZE_4k - len, "\033[%sm%s\033[0m", log_level_clr[lvl], log_level_shortname[lvl]);
	}

	return len;
}

static void
print_buffer(FILE *out, const char *s, uint32_t len, bool tty, bool progress)
{
	print_colorized(out, s, !tty);

	if (progress && s[len - 1] == '\n') {
		log_progress_print_bar(log_cfg.progress.prev_name);
	}
}

void
log_printn(enum log_level lvl, const char *buf, uint32_t len)
{
	if (log_cfg.debug_file) {
		print_buffer(log_cfg.debug_file, buf, len, false, false);
	}

	if (!log_should_print(lvl)) {
		return;
	}

	if (log_cfg.tstr) {
		tstr_pushn(0, log_cfg.tstr, buf, len);
		tstr_push(0, log_cfg.tstr, '\n');
	} else {
		print_buffer(log_cfg.file,
			buf,
			len,
			log_cfg.file_is_a_tty,
			log_cfg.progress_bar && (lvl == log_info || lvl == log_warn));
	}
}

void
log_printv(enum log_level lvl, const char *fmt, va_list ap)
{
	static char buf[BUF_SIZE_32k];
	vsnprintf(buf, ARRAY_LEN(buf) - 1, fmt, ap);
	log_printn(lvl, buf, strlen(buf));
}

void
log_print(bool nl, enum log_level lvl, const char *fmt, ...)
{
	static char buf[BUF_SIZE_4k + 3];

	uint32_t len = log_print_prefix(lvl, buf, BUF_SIZE_4k);

	va_list ap;
	va_start(ap, fmt);
	len += vsnprintf(&buf[len], BUF_SIZE_4k - len, fmt, ap);
	va_end(ap);

	if (nl && len < BUF_SIZE_4k) {
		buf[len] = '\n';
		buf[len + 1] = 0;
		++len;
	}

	if (log_cfg.progress_bar) {
		log_raw("\033[K");
	}

	log_printn(lvl, buf, len);
}

void
log_plain(enum log_level lvl, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_printv(lvl, fmt, ap);
	va_end(ap);
}

void
log_rawv(const char *fmt, va_list ap)
{
	vfprintf(log_cfg.file, fmt, ap);
}

void
log_raw(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_rawv(fmt, ap);
	va_end(ap);
}

void
log_plain_version_string(enum log_level lvl, const char *version)
{
	if (strcmp(version, "undefined") == 0) {
		return;
	}

	log_plain(lvl, " version: %s", version);
}

const char *
bool_to_yn(bool v)
{
	return v ? CLR(c_green) "YES" CLR(0) : CLR(c_red) "NO" CLR(0);
}

void
log_set_file(FILE *log_file)
{
	log_cfg.file = log_file;
	log_cfg.tstr = 0;
	log_cfg.file_is_a_tty = fs_is_a_tty(log_file);
}

void
log_set_debug_file(FILE *log_file)
{
	log_cfg.debug_file = log_file;
}

void
log_set_buffer(struct tstr *buf)
{
	assert(buf->flags & tstr_flag_overflow_alloc);

	log_cfg.tstr = buf;
	log_cfg.file = 0;
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
