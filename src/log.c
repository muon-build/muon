#include "posix.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "buf_size.h"
#include "log.h"

static char *log_level_clr[log_level_count] = {
	[log_error] = "31",
	[log_warn] = "33",
	[log_info] = "34",
	[log_debug] = "0",
};

static char *log_level_name[log_level_count] = {
	[log_error] = "error",
	[log_warn] = "warn",
	[log_info] = "info",
	[log_debug] = "dbg",
};

static struct {
	FILE *file;
	enum log_level level;
	uint32_t filter;
	bool initialized, clr;
	uint32_t opts;
} log_cfg = { .level = log_info, };

bool
log_should_print(enum log_level lvl)
{
	return lvl <= log_cfg.level;
}

void
log_print(const char *file, uint32_t line, const char *func, bool nl,
	enum log_level lvl, const char *fmt, ...)
{
	static char buf[BUF_SIZE_4k + 3];

	if (log_should_print(lvl)) {
		uint32_t len = 0;

		assert(log_cfg.initialized);

		if (log_cfg.clr) {
			len += snprintf(&buf[len], BUF_SIZE_4k - len, "\033[%sm%s\033[0m",
				log_level_clr[lvl], log_level_name[lvl]);
		} else {
			len = strlen(log_level_name[lvl]);
			strncpy(buf, log_level_name[lvl], BUF_SIZE_4k);
		}

		buf[len] = ' ';
		++len;

		if (log_cfg.opts & log_show_source) {
			if (log_cfg.clr) {
				len += snprintf(&buf[len], BUF_SIZE_4k - len,
					"%s:%d [\033[35m%s\033[0m] ", file, line, func);
			} else {
				len += snprintf(&buf[len], BUF_SIZE_4k - len,
					"%s:%d [%s] ", file, line, func);
			}
		}

		va_list ap;
		va_start(ap, fmt);
		len += vsnprintf(&buf[len], BUF_SIZE_4k - len, fmt, ap);
		va_end(ap);

		if (nl && len < BUF_SIZE_4k) {
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

	if ((sll = getenv("MUON_LOG_LVL"))) {
		ll = strtoul(sll, NULL, 10);
		log_set_lvl(ll);
	}

	log_cfg.file = stderr;
	log_cfg.clr = log_file_is_a_tty();
	log_cfg.initialized = true;
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
		L("attempted to set log level to invalid value %d (max: %d)", ll, log_level_count);
		return;
	}

	log_cfg.level = ll;
}

void
log_set_opts(enum log_opts opts)
{
	log_cfg.opts = opts;
}

FILE *
log_file(void)
{
	return log_cfg.file;
}
