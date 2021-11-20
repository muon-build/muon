#include "posix.h"

#include <stdarg.h>
#include <stdlib.h> // exit

#include "buf_size.h"
#include "error.h"
#include "log.h"

void
error_unrecoverable(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_plainv(fmt, ap);
	log_plain("\n");
	va_end(ap);

	exit(1);
}

void
error_message(struct source *src, uint32_t line, uint32_t col, const char *msg)
{
	const char *label = log_clr() ? "\033[31merror:\033[0m" : "error:";

	log_plain("%s:%d:%d: %s %s\n", src->label, line, col, label, msg);

	uint64_t i, cl = 1, sol = 0;
	for (i = 0; i < src->len; ++i) {
		if (src->src[i] == '\n') {
			++cl;
			sol = i + 1;
		}

		if (cl == line) {
			break;
		}
	}

	char line_pre[32] = { 0 };
	uint32_t line_pre_len = snprintf(line_pre, 31, "%3d | ", line);

	log_plain("%s", line_pre);
	for (i = sol; src->src[i] && src->src[i] != '\n'; ++i) {
		if (src->src[i] == '\t') {
			log_plain("        ");
		} else {
			putc(src->src[i], stderr);
		}
	}
	log_plain("\n");

	for (i = 0; i < line_pre_len; ++i) {
		log_plain(" ");
	}

	for (i = 0; i < col; ++i) {
		for (i = 0; i < col; ++i) {
			if (src->src[sol + i] == '\t') {
				log_plain("        ");
			} else {
				log_plain(i == col - 1 ? "^" : " ");
			}
		}
		log_plain("\n");
	}
}

void
error_messagev(struct source *src, uint32_t line, uint32_t col, const char *fmt, va_list args)
{
	static char buf[BUF_SIZE_4k];
	vsnprintf(buf, BUF_SIZE_4k, fmt, args);
	error_message(src, line, col, buf);
}

void
error_messagef(struct source *src, uint32_t line, uint32_t col, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	error_messagev(src, line, col, fmt, ap);
	va_end(ap);
}
