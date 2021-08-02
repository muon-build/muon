#include "posix.h"

#include <stdarg.h>
#include <stdlib.h> // exit

#include "log.h"
#include "error.h"

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
error_message(struct source *src, uint32_t line,
	uint32_t col, const char *fmt, va_list args)
{
	const char *label = log_clr() ? "\033[31merror:\033[0m" : "error:";

	log_plain("%s:%d:%d: %s ", src->label, line, col, label);
	log_plainv(fmt, args);
	log_plain("\n");

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

	log_plain("%3d | ", line);
	for (i = sol; src->src[i] && src->src[i] != '\n'; ++i) {
		if (src->src[i] == '\t') {
			log_plain("        ");
		} else {
			putc(src->src[i], stderr);
		}
	}
	log_plain("\n");

	log_plain("      ");
	for (i = 0; i < col; ++i) {
		if (src->src[sol + i] == '\t') {
			log_plain("        ");
		} else {
			log_plain(i == col - 1 ? "^" : " ");
		}
	}
	log_plain("\n");
}

void
error_messagef(struct source *src, uint32_t line, uint32_t col, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	error_message(src, line, col, fmt, ap);
	va_end(ap);
}
