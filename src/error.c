/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdarg.h>
#include <stdlib.h> // exit
#include <string.h>

#include "buf_size.h"
#include "datastructures/arr.h"
#include "error.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/mem.h"

static struct {
	struct arr messages;
	struct workspace *wk;
	enum error_diagnostic_store_replay_opts opts;
	bool init;
} error_diagnostic_store = { 0 };

void
error_diagnostic_store_init(struct workspace *wk)
{
	arr_init(&error_diagnostic_store.messages, 32, sizeof(struct error_diagnostic_message));
	error_diagnostic_store.init = true;
	error_diagnostic_store.wk = wk;
}

struct arr *
error_diagnostic_store_get(void)
{
	return &error_diagnostic_store.messages;
}

void
error_diagnostic_store_destroy(struct workspace *wk)
{
	uint32_t i;
	struct error_diagnostic_message *msg;
	for (i = 0; i < error_diagnostic_store.messages.len; ++i) {
		msg = arr_get(&error_diagnostic_store.messages, i);
		z_free((char *)msg->msg);
	}

	arr_destroy(&error_diagnostic_store.messages);

	memset(&error_diagnostic_store, 0, sizeof(error_diagnostic_store));
}

void
error_diagnostic_store_push(uint32_t src_idx, struct source_location location, enum log_level lvl, const char *msg)
{
	uint32_t mlen = strlen(msg);
	char *m = z_calloc(mlen + 1, 1);
	memcpy(m, msg, mlen);

	arr_push(&error_diagnostic_store.messages,
		&(struct error_diagnostic_message){
			.location = location,
			.lvl = lvl,
			.msg = m,
			.src_idx = src_idx,
		});
}

static int32_t
error_diagnostic_store_compare_except_lvl(const void *_a, const void *_b, void *ctx)
{
	const struct error_diagnostic_message *a = _a, *b = _b;
	int32_t v;

	if (a->src_idx != b->src_idx) {
		return (int32_t)a->src_idx - (int32_t)b->src_idx;
	} else if (a->location.off != b->location.off) {
		return (int32_t)a->location.off - (int32_t)b->location.off;
	} else if (a->location.len != b->location.len) {
		return (int32_t)a->location.len - (int32_t)b->location.len;
	} else if ((v = strcmp(a->msg, b->msg)) != 0) {
		return v;
	} else {
		return 0;
	}
}

static int32_t
error_diagnostic_store_compare(const void *_a, const void *_b, void *ctx)
{
	const struct error_diagnostic_message *a = _a, *b = _b;
	int32_t v;

	if ((v = error_diagnostic_store_compare_except_lvl(a, b, ctx)) != 0) {
		return v;
	} else if (a->lvl != b->lvl) {
		return a->lvl > b->lvl ? 1 : -1;
	} else {
		return 0;
	}
}

bool
error_diagnostic_store_replay(struct workspace *wk, enum error_diagnostic_store_replay_opts opts)
{
	error_diagnostic_store.init = false;
	error_diagnostic_store.opts = opts;

	uint32_t i;
	struct error_diagnostic_message *msg;
	struct source *last_src = 0, *cur_src;

	if (!error_diagnostic_store.messages.len) {
		return true;
	}

	arr_sort(&error_diagnostic_store.messages, NULL, error_diagnostic_store_compare);

	{
		struct arr filtered;
		arr_init(&filtered, 32, sizeof(struct error_diagnostic_message));
		arr_push(&filtered, arr_get(&error_diagnostic_store.messages, 0));
		struct error_diagnostic_message *prev_msg, tmp;
		for (i = 1; i < error_diagnostic_store.messages.len; ++i) {
			prev_msg = arr_get(&error_diagnostic_store.messages, i - 1);
			msg = arr_get(&error_diagnostic_store.messages, i);

			if (error_diagnostic_store_compare_except_lvl(prev_msg, msg, 0) == 0) {
				z_free((char *)msg->msg);
				continue;
			}

			tmp = *msg;
			arr_push(&filtered, &tmp);
		}

		arr_destroy(&error_diagnostic_store.messages);
		error_diagnostic_store.messages = filtered;
	}

	if (opts & error_diagnostic_store_replay_prepare_only) {
		return true;
	}

	/* ---------------------------------------------------------------------- */

	bool ok = true;

	enum error_message_flag flags = 0;
	if (opts & error_diagnostic_store_replay_dont_include_sources) {
		flags |= error_message_flag_no_source;
	}

	struct source src = { 0 }, null_src = {
		.label = "",
	};
	for (i = 0; i < error_diagnostic_store.messages.len; ++i) {
		msg = arr_get(&error_diagnostic_store.messages, i);

		if (opts & error_diagnostic_store_replay_werror) {
			msg->lvl = log_error;
		}

		if ((opts & error_diagnostic_store_replay_errors_only) && msg->lvl != log_error) {
			continue;
		}

		if (msg->lvl == log_error) {
			ok = false;
		}

		cur_src = msg->src_idx == UINT32_MAX ? &null_src :
						       arr_get(&wk->vm.src, msg->src_idx);

		if (cur_src != last_src) {
			if (!(opts & error_diagnostic_store_replay_dont_include_sources)) {
				if (last_src) {
					log_plain("\n");
				}

				log_plain("%s%s%s\n",
					log_clr() ? "\033[31;1m" : "",
					cur_src->label,
					log_clr() ? "\033[0m" : "");
			}

			last_src = cur_src;
			src = *cur_src;
		}

		error_message(&src, msg->location, msg->lvl, flags, msg->msg);
	}

	error_diagnostic_store_destroy(wk);

	return ok;
}

void
error_unrecoverable(const char *fmt, ...)
{
	va_list ap;

	if (log_clr()) {
		log_plain("\033[31m");
	}

	log_plain("fatal error");

	if (log_clr()) {
		log_plain("\033[0m");
	}

	log_plain(": ");
	va_start(ap, fmt);
	log_plainv(fmt, ap);
	log_plain("\n");
	va_end(ap);

	exit(1);
}

MUON_ATTR_FORMAT(printf, 3, 4)
static uint32_t
print_source_line(const struct source *src, uint32_t tgt_line, const char *prefix_fmt, ...)
{
	uint64_t i, line = 1, start_of_line = 0;
	for (i = 0; i < src->len; ++i) {
		if (src->src[i] == '\n') {
			++line;
			start_of_line = i + 1;
		}

		if (line == tgt_line) {
			break;
		}
	}

	if (i >= src->len) {
		return 0;
	}

	char prefix_buf[32] = { 0 };
	va_list ap;
	va_start(ap, prefix_fmt);
	uint32_t ret = vsnprintf(prefix_buf, sizeof(prefix_buf), prefix_fmt, ap);
	va_end(ap);

	log_plain("%s", prefix_buf);
	for (i = start_of_line; src->src[i] && src->src[i] != '\n'; ++i) {
		if (src->src[i] == '\t') {
			log_plain("        ");
		} else {
			log_plain("%c", src->src[i]);
		}
	}
	log_plain("\n");
	return ret;
}

void
get_detailed_source_location(const struct source *src,
	struct source_location loc,
	struct detailed_source_location *dloc,
	enum get_detailed_source_location_flag flags)
{
	*dloc = (struct detailed_source_location){
		.loc = loc,
		.line = 1,
		.col = 1,
	};

	if (!src || loc.off > src->len) {
		return;
	}

	uint32_t i, line = 1, end = loc.off + loc.len;
	for (i = 0; i < src->len; ++i) {
		if (i == loc.off) {
			dloc->col = (i - dloc->start_of_line) + 1;
		} else if (i == end) {
			dloc->end_col = i - dloc->start_of_line;
			return;
		}

		if (src->src[i] == '\n') {
			if (i + 1 == loc.off && loc.len == 1) {
				dloc->end_col = dloc->col = (i - dloc->start_of_line) + 1;
				dloc->line = line;
				return;
			}

			if (i > loc.off && !(flags & get_detailed_source_location_flag_multiline)) {
				dloc->loc.len = ((i - dloc->start_of_line) - dloc->col);
				return;
			}

			++line;

			if (i <= loc.off) {
				dloc->line = line;
			} else {
				dloc->end_line = line;
			}

			dloc->start_of_line = i + 1;
		}
	}
}

static void
list_line_underline(const struct source *src, struct detailed_source_location *dloc, uint32_t line_pre_len, bool end)
{
	uint32_t i;

	if (end) {
		line_pre_len -= 2;
	}

	for (i = 0; i < line_pre_len; ++i) {
		log_plain(" ");
	}

	if (end) {
		log_plain("|_");
	}

	uint32_t col;
	const char *tab, *space;
	if (end) {
		col = dloc->end_col;
		tab = "________";
		space = "_";
	} else {
		col = dloc->col;
		tab = "        ";
		space = " ";
	}

	for (i = 0; i < col; ++i) {
		if (dloc->start_of_line + i < src->len && src->src[dloc->start_of_line + i] == '\t') {
			log_plain("%s", tab);
		} else {
			log_plain("%s", i == col - 1 ? "^" : space);
		}
	}

	if (!end) {
		for (i = 1; i < dloc->loc.len; ++i) {
			log_plain("_");
		}
	}
	log_plain("\n");
}

void
reopen_source(const struct source *src, struct source *src_reopened, bool *destroy_source)
{
	*src_reopened = *src;
	if (!src->len) {
		switch (src->type) {
		case source_type_unknown: return;
		case source_type_embedded: UNREACHABLE; break;
		case source_type_file:
			if (!fs_read_entire_file(src->label, src_reopened)) {
				return;
			}
			*destroy_source = true;
			break;
		}
	}
}

void
list_line_range(const struct source *src, struct source_location location, uint32_t context)
{
	log_plain("-> %s%s%s\n", log_clr() ? "\033[32m" : "", src->label, log_clr() ? "\033[0m" : "");

	bool destroy_source = false;
	struct source src_reopened;
	reopen_source(src, &src_reopened, &destroy_source);

	struct detailed_source_location dloc;
	get_detailed_source_location(&src_reopened, location, &dloc, 0);

	int32_t i;
	for (i = -(int32_t)context; i <= (int32_t)context; ++i) {
		uint32_t line_pre_len;

		line_pre_len = print_source_line(
			&src_reopened, dloc.line + i, "%s%3d | ", i == 0 ? ">" : " ", dloc.line + i);

		if (i == 0) {
			list_line_underline(&src_reopened, &dloc, line_pre_len, false);
		}
	}

	if (destroy_source) {
		fs_source_destroy(&src_reopened);
	}
}

struct error_diagnostic_message_record {
	struct source_location location;
	enum log_level lvl;
	const struct source *src;
	char msg[BUF_SIZE_1k];
	uint32_t count;
	enum error_message_flag flags;
	bool was_emitted;
};

static struct error_diagnostic_message_record error_message_previously_emitted = { 0 };

void
error_message_flush_coalesced_message(void)
{
	if (!error_message_previously_emitted.src || error_message_previously_emitted.was_emitted) {
		return;
	}

	char buf[BUF_SIZE_1k] = { 0 };
	const char *msg = error_message_previously_emitted.msg;

	if (error_message_previously_emitted.count > 1) {
		snprintf(buf, sizeof(buf), "%s (%d times)", msg, error_message_previously_emitted.count);
		msg = buf;
	}

	error_message(error_message_previously_emitted.src,
		error_message_previously_emitted.location,
		error_message_previously_emitted.lvl,
		error_message_previously_emitted.flags,
		msg);

	error_message_previously_emitted = (struct error_diagnostic_message_record){0};
}

void
error_message(const struct source *src,
	struct source_location location,
	enum log_level lvl,
	enum error_message_flag flags,
	const char *msg)
{
	{
		enum error_message_flag flags_masked = flags & ~error_message_flag_coalesce;

		bool previous_matches = error_message_previously_emitted.lvl == lvl
					&& error_message_previously_emitted.location.off == location.off
					&& error_message_previously_emitted.location.len == location.len
					&& error_message_previously_emitted.flags == flags_masked
					&& error_message_previously_emitted.src == src
					/* && strcmp(error_message_previously_emitted.msg, msg) == 0 */
					;

		if (previous_matches) {
			++error_message_previously_emitted.count;
		} else {
			if (flags & error_message_flag_coalesce) {
				error_message_flush_coalesced_message();
			}

			error_message_previously_emitted = (struct error_diagnostic_message_record){
				.lvl = lvl,
				.location = location,
				.src = src,
				.count = 1,
				.flags = flags_masked,
			};
			snprintf(error_message_previously_emitted.msg,
				sizeof(error_message_previously_emitted.msg),
				"%s",
				msg);
		}

		if (flags & error_message_flag_coalesce) {
			return;
		}

		error_message_previously_emitted.was_emitted = true;
	}

	if (error_diagnostic_store.init) {
		if (src->len == 0 && src->src == 0) {
			// Skip messages generated for code regions with no
			// sources
			return;
		}

		uint32_t i;
		for (i = 0; i < error_diagnostic_store.wk->vm.src.len; ++i) {
			if (src == (struct source *)(arr_get(&error_diagnostic_store.wk->vm.src, i))) {
				break;
			}
		}
		assert(i < error_diagnostic_store.wk->vm.src.len);

		error_diagnostic_store_push(i, location, lvl, msg);
		return;
	}

	bool destroy_source = false;
	struct source src_reopened = { 0 };
	reopen_source(src, &src_reopened, &destroy_source);

	struct detailed_source_location dloc;
	get_detailed_source_location(&src_reopened, location, &dloc, get_detailed_source_location_flag_multiline);

	log_plain("%s:%d:%d: ", src_reopened.label, dloc.line, dloc.col);

	if (lvl != log_info) {
		if (log_clr()) {
			log_plain("\033[%sm%s\033[0m ", log_level_clr[lvl], log_level_name[lvl]);
		} else {
			log_plain("%s ", log_level_name[lvl]);
		}
	}

	log_plain("%s\n", msg);

	if (flags & error_message_flag_no_source) {
		goto ret;
	}

	uint32_t line_pre_len = 0;
	if (dloc.end_line) {
		for (uint32_t i = dloc.line; i <= dloc.end_line; ++i) {
			line_pre_len = print_source_line(&src_reopened, i, "%3d | %s ", i, i == dloc.line ? "/" : "|");
		}
		list_line_underline(&src_reopened, &dloc, line_pre_len, true);
	} else {
		if (!(line_pre_len = print_source_line(&src_reopened, dloc.line, "%3d | ", dloc.line))) {
			goto ret;
		}

		list_line_underline(&src_reopened, &dloc, line_pre_len, false);
	}

ret:
	if (destroy_source) {
		fs_source_destroy(&src_reopened);
	}
}

void
error_messagev(const struct source *src,
	struct source_location location,
	enum log_level lvl,
	const char *fmt,
	va_list args)
{
	static char buf[BUF_SIZE_4k];
	vsnprintf(buf, BUF_SIZE_4k, fmt, args);
	error_message(src, location, lvl, 0, buf);
}

void
error_messagef(const struct source *src, struct source_location location, enum log_level lvl, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	error_messagev(src, location, lvl, fmt, ap);
	va_end(ap);
}
