#include "posix.h"

#include <stdarg.h>
#include <stdlib.h> // exit
#include <string.h>

#include "buf_size.h"
#include "data/darr.h"
#include "error.h"
#include "log.h"
#include "platform/mem.h"

struct error_diagnostic_message {
	uint32_t line, col;
	enum log_level lvl;
	const char *msg;
	uint32_t src_idx;
};

struct error_diagnostic_source {
	struct source src;
	uint64_t id;
};

static struct {
	struct darr messages;
	struct darr sources;
	bool init;
} error_diagnostic_store;

void
error_diagnostic_store_init(void)
{
	darr_init(&error_diagnostic_store.messages, 32, sizeof(struct error_diagnostic_message));
	darr_init(&error_diagnostic_store.sources, 4, sizeof(struct error_diagnostic_source));
	error_diagnostic_store.init = true;
}

uint32_t
error_diagnostic_store_push_src(struct source *src)
{
	uint32_t i;
	struct error_diagnostic_source *s = NULL;
	for (i = 0; i < error_diagnostic_store.sources.len; ++i) {
		s = darr_get(&error_diagnostic_store.sources, i);
		// TODO: this is not super robust, as it relies on chance that
		// two sources don't get allocated at the same place
		if (s->id == (uint64_t)src && strcmp(s->src.label, src->label) == 0) {
			break;
		} else {
			s = NULL;
		}
	}

	if (!s) {
		struct source dup;
		fs_source_dup(src, &dup);

		darr_push(&error_diagnostic_store.sources, &(struct error_diagnostic_source) {
			.src = dup,
			.id = (uint64_t)src,
		});

		i = error_diagnostic_store.sources.len - 1;
	}

	s = darr_get(&error_diagnostic_store.sources, i);
	return i;
}

void
error_diagnostic_store_push(uint32_t src_idx, uint32_t line, uint32_t col, enum log_level lvl, const char *msg)
{
	uint32_t mlen = strlen(msg);
	char *m = z_calloc(mlen + 1, 1);
	memcpy(m, msg, mlen);

	darr_push(&error_diagnostic_store.messages,
		&(struct error_diagnostic_message){
		.line = line,
		.col = col,
		.lvl = lvl,
		.msg = m,
		.src_idx = src_idx,
	});
}

static int32_t
error_diagnostic_store_compare(const void *_a, const void *_b, void *ctx)
{
	const struct error_diagnostic_message *a = _a, *b = _b;

	if (a->src_idx != b->src_idx) {
		return (int32_t)a->src_idx - (int32_t)b->src_idx;
	} else if (a->line != b->line) {
		return (int32_t)a->line - (int32_t)b->line;
	} else if (a->col != b->col) {
		return (int32_t)a->col - (int32_t)b->col;
	} else if (a->lvl != b->lvl) {
		return (int32_t)a->lvl - (int32_t)b->lvl;
	} else {
		return strcmp(a->msg, b->msg);
	}
}

void
error_diagnostic_store_replay(bool errors_only)
{
	error_diagnostic_store.init = false;

	uint32_t i;
	struct error_diagnostic_message *msg;
	struct error_diagnostic_source *last_src = NULL, *cur_src;

	darr_sort(&error_diagnostic_store.messages, NULL, error_diagnostic_store_compare);

	size_t tail;
	if (error_diagnostic_store.messages.len > 1) {
		struct error_diagnostic_message *prev_msg, tmp;
		tail = error_diagnostic_store.messages.len;

		uint32_t initial_len = error_diagnostic_store.messages.len;
		msg = darr_get(&error_diagnostic_store.messages, 0);
		darr_push(&error_diagnostic_store.messages, msg);
		for (i = 1; i < initial_len; ++i) {
			prev_msg = darr_get(&error_diagnostic_store.messages, i - 1);
			msg = darr_get(&error_diagnostic_store.messages, i);

			if (error_diagnostic_store_compare(prev_msg, msg, NULL) == 0) {
				continue;
			}

			tmp = *msg;
			darr_push(&error_diagnostic_store.messages, &tmp);
		}
	} else {
		tail = 0;
	}

	for (i = tail; i < error_diagnostic_store.messages.len; ++i) {
		msg = darr_get(&error_diagnostic_store.messages, i);

		if (errors_only && msg->lvl != log_error) {
			continue;
		}

		if ((cur_src = darr_get(&error_diagnostic_store.sources, msg->src_idx)) != last_src) {
			if (last_src) {
				log_plain("\n");
			}

			log_plain("%s%s%s\n",
				log_clr() ? "\033[31;1m" : "",
				cur_src->src.label,
				log_clr() ? "\033[0m" : "");
			last_src = cur_src;
		}

		error_message(&cur_src->src, msg->line, msg->col, msg->lvl, msg->msg);

		z_free((char *)msg->msg);
	}

	for (i = 0; i < error_diagnostic_store.sources.len; ++i) {
		cur_src = darr_get(&error_diagnostic_store.sources, i);
		fs_source_destroy(&cur_src->src);
	}

	darr_destroy(&error_diagnostic_store.messages);
	darr_destroy(&error_diagnostic_store.sources);
}

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
error_message(struct source *src, uint32_t line, uint32_t col, enum log_level lvl, const char *msg)
{
	if (error_diagnostic_store.init) {
		error_diagnostic_store_push(error_diagnostic_store_push_src(src), line, col, lvl, msg);
		return;
	}

	log_plain("%s:%d:%d: ", src->label, line, col);

	if (log_clr()) {
		log_plain("\033[%sm%s\033[0m ", log_level_clr[lvl], log_level_name[lvl]);
	} else {
		log_plain("%s ", log_level_name[lvl]);
	}

	log_plain("%s\n", msg);

	if (!src->len) {
		return;
	}

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
error_messagev(struct source *src, uint32_t line, uint32_t col, enum log_level lvl, const char *fmt, va_list args)
{
	static char buf[BUF_SIZE_4k];
	vsnprintf(buf, BUF_SIZE_4k, fmt, args);
	error_message(src, line, col, lvl, buf);
}

void
error_messagef(struct source *src, uint32_t line, uint32_t col, enum log_level lvl, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	error_messagev(src, line, col, lvl, fmt, ap);
	va_end(ap);
}
