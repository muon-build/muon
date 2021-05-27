#include "posix.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

static char *log_level_clr[log_level_count] = {
	[log_warn] = "31",
	[log_info] = "34",
	[log_debug] = "0",
};

static char *log_level_name[log_level_count] = {
	[log_warn] = "warn",
	[log_info] = "info",
	[log_debug] = "dbg",
};

static char *log_filter_name[log_filter_count] = {
	[log_misc]     = "misc",
	[log_mem]      = "mem",
	[log_tok]      = "tok",
	[log_lex]      = "lex",
	[log_parse]    = "parse",
	[log_interp]   = "interp",
	[log_out]      = "output",
	[log_fetch]    = "fetch",
};

static uint32_t log_filter_bit[log_filter_count] = {
	[log_misc]     = (1 << 0),
	[log_mem]      = (1 << 1),
	[log_tok]      = (1 << 2),
	[log_lex]      = (1 << 3),
	[log_parse]    = (1 << 4),
	[log_interp]   = (1 << 5),
	[log_out]      = (1 << 6),
	[log_fetch]    = (1 << 7),
};

static struct {
	FILE *file;
	enum log_level level;
	uint32_t filter;
	bool initialized, clr;
	uint32_t opts;
} log_cfg = { .level = log_info, };

#define BUF_LEN 512

static bool
should_print(enum log_level lvl, enum log_filter type)
{
	return lvl <= log_info
	       || (log_cfg.level >= lvl && log_cfg.filter & log_filter_bit[type]);
}


void
log_print(const char *file, uint32_t line, const char *func, enum log_level lvl,
	enum log_filter type, const char *fmt, ...)
{
	static char buf[BUF_LEN + 3];

	if (should_print(lvl, type)) {
		uint32_t len = 0;

		assert(log_cfg.initialized);

		if (log_cfg.clr) {
			len += snprintf(&buf[len], BUF_LEN - len, "\033[%sm%s\033[0m",
				log_level_clr[lvl], log_level_name[lvl]);
		} else {
			len = strlen(log_level_name[lvl]);
			strncpy(buf, log_level_name[lvl], BUF_LEN);
		}

		if (type != log_misc) {
			buf[len] = ':';
			++len;
			strncpy(&buf[len], log_filter_name[type], BUF_LEN - len);
			len += strlen(log_filter_name[type]);
		}

		buf[len] = ' ';
		++len;

		if (log_cfg.opts & log_show_source) {
			if (log_cfg.clr) {
				len += snprintf(&buf[len], BUF_LEN - len,
					"%s:%d [\033[35m%s\033[0m] ", file, line, func);
			} else {
				len += snprintf(&buf[len], BUF_LEN - len,
					"%s:%d [%s] ", file, line, func);
			}
		}

		va_list ap;
		va_start(ap, fmt);
		len += vsnprintf(&buf[len], BUF_LEN - len, fmt, ap);
		va_end(ap);

		if (len < BUF_LEN) {
			buf[len] = '\n';
			buf[len + 1] = 0;
		}

		fputs(buf, log_cfg.file);
	}
}

void
log_plainv(const char *fmt, va_list ap)
{
	vfprintf(log_cfg.file, fmt, ap);
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
	char *sll;
	uint64_t ll;

	assert(!log_cfg.initialized);

	if ((sll = getenv("BOSON_LOG_LVL"))) {
		ll = strtoul(sll, NULL, 10);
		log_set_lvl(ll);
	}

	log_cfg.file = stderr;
	log_cfg.clr = log_file_is_a_tty();
	log_cfg.initialized = true;
}

void
log_bytes(const void *src, size_t size)
{
	const char *bytes = src;
	int32_t i, j;

	for (i = size - 1; i >= 0; --i) {
		for (j = 7; j >= 0; --j) {
			fprintf(log_cfg.file, "%c", bytes[i] & (1 << j) ? '1' : '0');
		}
		fprintf(log_cfg.file, " ");
	}
}

void
log_bytes_r(const void *src, size_t size)
{
	const char *bytes = src;
	uint32_t i, j;

	for (i = 0; i < size; ++i) {
		for (j = 0; j < 8; ++j) {
			fprintf(log_cfg.file, "%c", bytes[i] & (1 << j) ? '1' : '0');
		}
		fprintf(log_cfg.file, " ");
	}
}

bool
log_file_is_a_tty(void)
{
	int fd;
	return (fd = fileno(log_cfg.file)) != -1 && isatty(fd) == 1;
}

void
log_set_file(FILE *log_file)
{
	log_cfg.file = log_file;
	log_cfg.clr = log_file_is_a_tty();
}

void
log_set_lvl(enum log_level ll)
{
	if (ll > log_level_count) {
		L(log_misc, "attempted to set log level to invalid value %d (max: %d)", ll, log_level_count);
		return;
	}

	log_cfg.level = ll;
}

void
log_set_opts(enum log_opts opts)
{
	log_cfg.opts = opts;
}

void
log_set_filters(enum log_filter f)
{
	log_cfg.filter = f;
}

uint32_t
log_filter_to_bit(enum log_filter f)
{
	return log_filter_bit[f];
}

bool
log_filter_name_to_bit(const char *name, uint32_t *res)
{
	if (strcmp(name, "all") == 0) {
		*res = 0xffffffff;
		return true;
	}

	uint32_t i;
	for (i = 0; i < log_filter_count; ++i) {
		if (strcmp(name, log_filter_name[i]) == 0) {
			*res = log_filter_bit[i];
			return true;
		}
	}

	return false;
}
